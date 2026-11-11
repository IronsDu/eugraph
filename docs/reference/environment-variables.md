# 环境变量参考

> 运行期环境变量的统一入口。代码中的变量名集中定义在
> [`src/common/config/env_vars.hpp`](../../src/common/config/env_vars.hpp)；
> 新增运行期变量时，请同时更新本页和该头文件。

环境变量在进程/语句启动时读取。标注“启动时”的变量修改后需要重启 server；
标注“每条语句”的变量对之后的语句生效。

## 1. 诊断与 trace

| 变量 | 取值 | 读取点/范围 | 作用 | 开销/注意 |
|---|---|---|---|---|
| `EUGRAPH_OP_TRACE` | 非空 | 每条语句的 `QueryContext` | 每个物理算子输出 `[op-trace]` 行，语句结束输出树 | 每个算子多一层协程和计时；大结果集日志量很大 |
| `EUGRAPH_IO_TRACE` | 非空 | `AsyncGraphDataStore` 实例/语句 | 语句结束输出 IO dispatch、WT search/search_near 计数 | 计数器为原子操作；只在计数非零时输出 |
| `EUGRAPH_PLAN_DEBUG` | 任意值 | 物理规划 | 输出 `[PLAN]` 物理算子树和 slot layout | 只在 `planBound` 路径输出；Cascades 路径可能看不到 |
| `EUGRAPH_PRED_DEBUG` | 任意值 | 物理规划完成后 | 输出 `[PRED]` 每个 Filter 的谓词描述 | 只覆盖 Filter；每个谓词一行 |
| `EUGRAPH_DPL_DEBUG` | 任意值 | requirements 收集 | 输出 `[DPL]` 每个 slot 的物化需求 | 用于解释为什么整顶点被物化；只应在排查时开启 |

这些开关默认关闭。不要在生产常开，也不要用开启 trace 时的 wall time 做性能基准。

## 2. 查询/执行调优

| 变量 | 默认值 | 读取点 | 作用 | 备注 |
|---|---:|---|---|---|
| `EUGRAPH_OPTIMIZER_FORCE_RBO` | 关闭 | 优化器 | 值为 `1` 时跳过 CBO，强制 RBO 计划 | A/B 诊断用 |
| `EUGRAPH_LJ_BATCH` | 512 | LeftJoin 执行 | 相关 LeftJoin 的批大小；0 关闭批处理 | 建议 <1024；会受 chunk capacity 影响 |
| `EUGRAPH_SJ_BATCH` | 512 | SemiJoin 执行 | 相关 SemiJoin 的批大小；0 关闭批处理 | 仅相关列可批处理时生效 |
| `EUGRAPH_CACHE_MAX_EDGES_PER_SOURCE` | 16384 | Expand 执行 | 单个 source 的邻接缓存上限；超过则绕过 | 0 或非法值回退默认 |
| `EUGRAPH_PROBE_MAX_EDGES_PER_SOURCE` | 16384 | Expand 集合探针 | 单个目的端可物化的邻接边上限；超过则回退批量精确 seek | 0 或非法值回退默认；结果不变 |
| `EUGRAPH_PROBE_MAX_CACHED_EDGES` | 262144 | Expand 集合探针 | 探针缓存的总边预算；超过后未缓存目的端回退批量精确 seek | 0 或非法值回退默认；结果不变 |
| `EUGRAPH_EXPAND_INPUT_ROWS` | 128 | Expand 执行 | 一次 Expand 调用的最大输入行数；上限 4096 | 控制单次展开的 chunk 大小 |
| `EUGRAPH_INDEX_BACKFILL_BATCH` | 20000 | 索引回填 | 索引回填事务的行数；上限 1,000,000 | 0 或非法值回退默认 |
| `EUGRAPH_PATH_COUNT_CACHE_MAX_ENTRIES` | 1,048,576 | PathCount 执行 | 每个 PathCount 算子的 `VertexId -> outdegree` 缓存上限；0 禁用缓存 | 超限后不再插入，改为重新计算 degree；结果不变 |
| `EUGRAPH_NUMA_BIND` | 关闭 | 线程创建（启动时） | 值为 `1` 时启用 NUMA 绑定 | 单 NUMA 节点上是 no-op；绑定失败有日志 |

PathCount 的缓存是**每个算子实例**独立的；并发查询或多个 PathCount 会按算子数量叠加。
邻接计数本身使用流式 `scanEdges`（4096 条/批），不会保留完整超级节点邻接 vector。

## 3. 测试/脚本环境变量

以下变量只由测试脚本或 harness 使用，不属于 server 运行配置：

| 变量 | 使用方 | 作用 |
|---|---|---|
| `EUGRAPH_HOST` / `EUGRAPH_PORT` | TCK steps / `run_tck.py` | TCK 连接地址 |
| `EUGRAPH_BOLT_PORT` | Bolt driver 测试 | 测试 server 的 Bolt 端口 |
| `EUGRAPH_SERVER_BINARY` | Web console 测试 | 指定测试 server 二进制 |
| `EUGRAPH_DRIVER_NODE_CACHE` | Node driver 测试 | Node 依赖缓存目录 |
| `EUGRAPH_DRIVER_VENV_CACHE` | Python driver 测试 | Python venv 缓存目录 |
| `EUGRAPH_TEST_READY_TIMEOUT` | 测试启动脚本 | 等待 server 就绪的秒数 |
| `EUGRAPH_TSAN_DATA` / `EUGRAPH_TSAN_WORK` | `scripts/check_tsan.sh` | TSAN 数据/工作目录 |

`EUGRAPH_ENABLE_WEB_CONSOLE` 是 CMake 编译选项，不是环境变量。

## 4. 新增约定

新增运行期环境变量时：

1. 在 `src/common/config/env_vars.hpp` 添加 `kEnv...` 常量；
2. 业务代码通过该常量调用 `std::getenv`，不要散落字符串；
3. 在本页对应分类补充名称、类型、默认值、读取点和开销；
4. 涉及性能/资源的变量必须说明上限、回退行为和并发放大效应。
