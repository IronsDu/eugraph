# Bolt 驱动集成测试：设施说明与已知缺陷排查记录

> 适用：`bolt_python_driver_integration_tests` / `bolt_java_driver_integration_tests` /
> `bolt_js_ws_driver_integration_tests`（三个官方驱动兼容性测试）。
> 本文记录**测试设施**约定与一次完整排查的根因链，避免后续重复踩坑。

## 一、测试设施约定

| 约定 | 说明 |
|---|---|
| 每个驱动测试**独占端口组** | CMake 为三者分别指定 `BOLT_PORT/THRIFT_PORT`（17690/17691/17692 + 19090/19091/19092） |
| 三者**互斥 + 独占**运行 | `RESOURCE_LOCK "bolt_driver_port"` + `RUN_SERIAL TRUE`（JVM/Node/多版本驱动对并发敏感） |
| 包装脚本**必须**： | ① 就绪等待足够长（ASan 构建慢，现为 **180s**）；② **失败时保留 `WORK_DIR`**（内含 `bolt-server.log`）；③ **退出码必须传播**（`cleanup()` 末尾 `exit $rc`，否则 trap 里 `rm -rf` 成功会把脚本退出码覆盖成 0 ⇒ **谎报成功**）；④ 内层测试加**硬超时** + 服务端 `setsid` **独立进程组**并按组清理（否则孙进程继承 ctest 的 stdout/stderr 管道 ⇒ **ctest 在测试超时后仍无限阻塞**） |
| Java 驱动的 jar | CMake 按 **glob `neo4j-java-driver-5.28.*.jar`** 探测（**不要钉死小版本**：曾因 5.28.14 vs 5.28.15 导致该测试**静默不被注册**） |
| 测试数据隔离 | 测试自带 `DROP DATABASE` + `CREATE DATABASE`（py/js）；**每次运行也可用唯一库名**（前缀区分驱动，如 `bolt_py_<uuid>`）以避免与残留/并发互相干扰 |

## 二、一次排查的根因链（2026-10，非索引缺陷）

**现象**：全新服务端上 **第一遍 29/36 失败** ✗、**第二遍 35/36 通过** ✓；断言形如 `assert 0 == 1`（记录为空）。
**注意**：这**不是** `Path` 兼容性问题，也**不是**测试数据污染（单独跑/整文件跑/重复跑均可通过或失败，取决于服务端状态）。

**逐层判据与结论**：

1. **提交结果被丢弃** ✗ —— `bolt_session.cpp`（自动提交、显式 COMMIT）与 `eugraph_handler.cpp`（Thrift 流）
   都 `co_await …commitTran(…)` 而**不看返回值**（对照 `query_executor.cpp` 是检查的）⇒ 存储出错时
   **静默丢写入**（数据丢失级）。**已修**：三处一律检查，失败即回 `FAILURE DatabaseError` / 抛异常 ⇒
   客户端能看到 `Transaction commit failed (storage engine error)`。
2. **不是 WT 提交失败** ✗ —— 日志中 `WiredTiger commit failed` 计数 = 0 ⇒ 走的是
   `SyncGraphDataStore::commitTransaction` 里"句柄不在 `txns_`"的**静默 `return false`** 分支。**已加诊断**。
3. **句柄本身为空** ✓✓ —— 诊断显示 `handle=0x0`（`INVALID_GRAPH_TXN`）⇒ 语句写入**从未进入事务**
   ⇒ 自然不落库。**根因方向确定**：**新建数据库后的语句路径使用了空事务句柄**。
4. **`beginTransaction` 未报错** ✗ —— `Failed to open transaction session` / `Failed to begin transaction`
   计数均为 0 ⇒ 要么 begin 从未被调用，要么其结果未被使用。**下一步观测点**：语句开始处打印
   `txn` 与图/store 解析结果。

**另一条独立缺陷**：`DROP DATABASE` 在**锁外**关闭三个 store（`graph_manager.cpp` 注释自述
"release the lock before blocking close"），而在飞协程可能仍持有该图实例 ⇒ WT 连接被并发使用 ⇒
`__conn_close … failure during close` + **WT_PANIC** + `disabling further writes`（此后进程内写入全部丢失）。
**修法**（与在线索引构建同款）：**先关闸（拒绝新使用）→ 等在飞使用归零 → 再关连接**。
