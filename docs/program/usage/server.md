# Server 使用指南

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

---

## 启动

```bash
eugraph-server --thrift-port 9090 --data-dir ./eugraph-data --compute-threads 4
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--thrift-port` | 9090 | Thrift RPC 监听端口（`eugraph-shell` / `eugraph-loader` 连接此端口） |
| `--bolt-port` | 7687 | Neo4j Bolt 协议端口；`0` 表示禁用 |
| `--web-port` | 7475 | 内嵌 Web Console 端口；`0` 表示禁用；Bolt 禁用时自动关闭 |
| `--web-bind` | 127.0.0.1 | Web Console 绑定地址（当前仅 IPv4） |
| `--web-allowed-origin` | （空） | 额外允许的浏览器 WebSocket Origin，可重复 |
| `--data-dir, -d` | `./eugraph-data` | 数据存储目录 |
| `--compute-threads` | 4 | 查询计算线程数（进程共享 `CPUThreadPoolExecutor`）；Bolt session 与 Thrift 查询的解析/计划/算子执行都在此池 |
| `--storage-io-threads` | 4 | 存储 IO 线程数（`IoScheduler` 的 IO 线程池） |
| `--thrift-io-threads` | 4 | Thrift IO 线程数；该池同时被用作 handler 执行池 |
| `--bolt-io-threads` | 1 | Bolt EventBase 线程数 |
| `--wt-cache-size-mb` | 256 | WiredTiger data cache 大小（MB） |
| `--wt-evict-threads-max` | 4 | WiredTiger 最大 eviction 线程数 |
| `--wt-txn-sync` | fsync | WiredTiger commit 同步策略：`fsync` 或 `none` |

`--compute-threads` 控制查询计算线程池；`--storage-io-threads` 与 `--thrift-io-threads` 分别控制存储 IO 池和 Thrift IO/handler 池（均默认 4）。数据文件分别在 `{data_dir}/data` 和 `{data_dir}/meta`。

### 存储调度模式（`EUGRAPH_STORAGE_DISPATCH_MODE` / `EUGRAPH_IO_MODE`）

数据操作的存储调用落在哪个执行器上由构建期 `EUGRAPH_STORAGE_DISPATCH_MODE`（CMake cache，
默认 **`same`**）决定，运行时可用 `EUGRAPH_IO_MODE=pool|inline|same` 覆盖做 A/B：

| 模式 | 语义 | 适用 |
|---|---|---|
| `same`（默认） | 投递到**调用方自己的 compute 池**：调用者在存储调用期间被挂起、不占 slot，计算与存储共享同一个容量池 | 默认；容量按 `--compute-threads` 计算 |
| `pool` | 投递到独立的 `--storage-io-threads` 池（历史设计） | 需要把存储与计算线程硬隔离时 |
| `inline` | 在调用线程上同步执行，无 hop、cache 局部性最好，但整段存储调用阻塞该 compute 线程 | 每查询独占核的部署 |

要点：

- **容量账**：一条查询任一时刻只占 1 个 worker，所以单池模式的并发上限 ≈ `--compute-threads`。
  建议 `--compute-threads ≈ 预期峰值并发 × 1.5`（实测 8 线程可跑 8~16 并发；再往上加线程收益递减，
  因为瓶颈转为 CPU/缓存带宽）。
- **`BEGIN/COMMIT/ROLLBACK` 始终走 `--storage-io-threads` 池**：commit 可能触发 fsync，
  不能占用查询 slot。因此即使数据操作全内联，`--storage-io-threads` 仍建议保留 ≥1。
- 快慢混合：慢查询只占 1 个 slot，快查询的损失近似其容量份额（2/16 ≈ 12%），不会把快查询卡死；
  线程数不足（在飞查询 > 线程数）时才明显排队。
- 启动日志会打印生效模式：`Storage dispatch: same (compute threads 8, storage IO threads 4; ...)`。
- 实测（SF1 complex-only、官方 LDBC driver、同二进制 `EUGRAPH_IO_MODE` 交错两轮）：
  **2 线程 3.71–3.79 vs 2.93 op/s（+27%）；4 线程 6.44–6.60 vs 3.90–3.99 op/s（+66%）**。
  分析细节见 `docs/benchmark/ldbc-snb-concurrency.md`（PR #311 分支）。

线程归属：Thrift IO、Storage IO、Compute 是**三个独立线程池**。Thrift / RPC 模式下网络 IO、fbthrift 流循环与序列化在 `--thrift-io-threads` 池；全部 RPC handler body 经 `async_tm_*` 调度到 `--compute-threads` 池，完成回调显式投回连接 EventBase（否则请求-响应 RPC 会因 fbthrift WriteBatcher 停顿 5–7s）。数据操作的存储调用按上面的“存储调度模式”执行（默认回到 Compute 池）；会话控制
（BEGIN/COMMIT/ROLLBACK）固定在 `--storage-io-threads` 池。Bolt 模式下 session 处理也被调度到 Compute 池。详见 [运行时执行模型](../../query/engine/execution-model.md)。

> `--wt-txn-sync none` 会关闭 WiredTiger commit 级 fsync，写入更快，但崩溃时可能丢失最近一段事务。
> 适合纯导入场景；正式在线服务建议保持默认 `fsync`。

## 典型流程

```bash
# 1. 启动 server
eugraph-server -d ./eugraph-data

# 2. 导入数据（schema 是必填的唯一声明入口；--graph 指定装到哪张图，不存在会自动创建）
eugraph-loader --schema ./docs/program/design/ldbc-sf01-composite.schema.json \
    --data-dir ./social_network-sf0.1-CsvComposite-LongDateFormatter \
    --host 127.0.0.1 --port 9090 --graph default

# 3. 连接 shell
eugraph-shell --host 127.0.0.1 --port 9090
```

## Web Console

```bash
eugraph-server -d ./eugraph-data
# 浏览器打开 http://127.0.0.1:7475（默认端口；--web-port 0 可关闭）
```

Web Console 默认开启，端口 7475（避开 Neo4j Browser 常用的 7474）。它与 Bolt 服务同进程：
静态资产编译进二进制，查询通过 Bolt-over-WebSocket 执行，不经过额外 HTTP 查询层。默认只监听回环；
非回环部署需同时配置 `--web-bind` 与 `--web-allowed-origin`。设计与打包流程见
[service/web-console.md](../../service/web-console.md)。

loader 会**先连服务器并做一次真实 RPC 握手**，再解析 schema；端口写错会立刻报错。

## 架构参考

设计文档见 [server-shell-design.md](../design/server-shell-design.md)。
