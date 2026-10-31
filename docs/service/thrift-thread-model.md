# Thrift 线程模型：计算 / 网络 / 存储 IO 分离

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

## 1. 目标

Thrift RPC 模式下，以下三类工作必须落在不同的 executor 上：

| 工作 | executor | 规模参数 |
|------|----------|---------|
| 网络 IO、fbthrift 流循环、结果序列化 | Thrift IO 池（`ThriftIO`） | `--thrift-io-threads` |
| handler body：参数解析、DDL、batch import、Cypher parse/bind/plan/算子执行 | Compute 池（`eugraph-compute`，进程共享） | `--compute-threads` |
| WiredTiger 读写 | Storage IO 池（`eugraph-io`） | `--storage-io-threads` |

Compute 池由 `GraphManager` 持有，通过 `QueryExecutor::Config::compute_pool` 注入每个图，
并由 `GraphService::computeExecutor()` 暴露给协议层。

## 2. 为什么不能直接把 handler ThreadManager 指向 Compute

fbthrift 5.05.19 的 `HandlerCallbackBase::putMessageInReplyQueue()` 在回包由
**非连接 EventBase 线程**产生时，把回包放进 `IOWorkerContext::ReplyQueue`
（`EventBaseAtomicNotificationQueue`）。`ReplyQueue` 的通知本身很快（gdb 实测
`notifyFd → execute → QueueReplyInfo → RocketServerConnection::send` 约 15 ms），
但 `RocketServerConnection::WriteBatcher` 的 flush 依赖 EventBase 的下一次唤醒；
空闲请求-响应连接的下一次唤醒是 5 s 的 task-expire timer，于是 RPC 回包延迟 **5–7 s**。
流式查询不受影响，因为 stream payload 持续唤醒 EventBase——只测流式查询会得到假阴性。

实测（临时探针，debug，见 git 历史/排查记录）：

| 配置 | `listLabels` 中位 | 流式查询 |
|------|------------------:|---------:|
| handler TM = 网络 IO 池（1/4 线程） | 0.4–1.1 ms | 1–2 ms |
| handler TM = Compute 池（1/4 网络线程） | **5.9–6.1 s** | 1.9–2.3 ms |
| handler TM = 独立 IO 池 / 默认 Simple TM | **5.9–6.5 s** | 1.9–2.2 ms |
| 仅查询 offload + 回原 IO 池（池内多线程） | 视恢复到的 worker 而定，可能 5 s+ | 正常 |

结论：**“回包必须在连接自己的 EventBase 线程产生”**，而不是“在同一个池里”或
“在 Compute 池上”。只把 storage dispatch 改成真正跨线程（`co_withExecutor`）后，
handler 协程从 Storage IO 返回时会恢复到 IO 池里的任意 worker，同样触发停顿；因此
必须显式把完成回调投回连接 EB。

## 3. 实现

### 3.1 全部 handler 走 async_tm

`EuGraphHandler` 覆盖全部 `async_tm_*` 方法（createGraph/dropGraph/listGraphs/
createLabel/listLabels/createEdgeLabel/listEdgeLabels/batchInsertVertices/
batchInsertEdges/executeCypher）。每个 override：

1. 在连接 EventBase 线程上被 fbthrift 调用（`getEventBase()` 即该连接 EB）；
2. 通过 `runOnComputeAndComplete()` 把一个 Task 调度到 Compute 池；
3. Task 完成后，回调通过 `runInEventBaseThread()` 投回连接 EB，再调用
   `HandlerCallback::complete()` —— 回包走 fbthrift 的 inline fast path。

`runOnComputeAndComplete` 在 `src/service/thrift/eugraph_handler.cpp` 的匿名 namespace，
签名：

```cpp
template <typename T, typename Fn>
void runOnComputeAndComplete(apache::thrift::HandlerCallbackPtr<T> cb,
                             folly::Executor::KeepAlive<> compute, Fn fn);
```

**兼容性注意**：覆盖 `async_tm_*` 会绕过 fbthrift 的 service interceptors 与
`InvocationType` 协商。当前服务端没有安装 interceptor，且所有方法都有 coroutine 实现；
如果将来引入鉴权/审计 interceptor，需要改为在 helper 里显式调用，或回到
`co_executeCypher(RequestParams, ...)` 并自行处理回复线程。

### 3.2 查询流：planning/算子在 Compute，流循环/序列化在 IO

`co_executeCypherImpl` 在 Compute 池执行参数解析与
`GraphService::executeCypher()`（parse → bind → plan → `prepareStream`）。
在 async_tm 路径，它先在 Compute 预取 **第一个 batch**（`prefetch_first_chunk`），
再交给 `service::hopGeneratorTo(gen, first, exhausted, compute)`：

```cpp
if (first) co_yield std::move(*first);          // 已在 Compute 算好
if (exhausted) co_return;                       // 空结果：generator 已 done
while (auto chunk = co_await folly::coro::co_viaIfAsync(compute.get_alias(), inner.next()))
    co_yield std::move(*chunk);
```

- 第一个 batch 的同步算子工作因此在 Compute；空结果不会对已 done 的 generator
  再调 `next()`（会触发 AsyncGenerator 的 `!coro_.done()` 检查）；
- 后续 batch：`AsyncGenerator::next()` 的 `viaIfAsync()` 在 resume 前把 generator
  promise 的 executor 设为 Compute；generator 的第一个 `co_await`（扫描的存储 dispatch）
  之后在 Compute 恢复，但从 yield 恢复后的第一段同步代码仍可能先在 Thrift IO 上执行；
- wrapper 本身由 fbthrift 的流循环在 Thrift IO 线程消费，`ResultRowBatch`/Thrift
  序列化也在 IO；
- 没有用“每个 next() 包一个 Task + co_withExecutor”：那能保证每段都在 Compute，
  但会把协程帧从流上摘下来；fbthrift 取消/销毁流时可能触到已释放状态。`viaIfAsync`
  保持结构化，是探针验证过的形态；
- 首 batch 预取失败（SKIP 负数、非法参数、运行期类型错误等）时**不回退成方法级异常**：
  handler 返回一个 `errorGenerator`，在第一次 pull 时重抛，保持“runtime 错误”的
  阶段/类型语义（TCK 依赖它；否则 46 个 “an error should be raised at runtime”
  场景会退化为 compile-time）。
- 代价：首包要等第一个 batch 算完才返回（阻塞算子会延后 metadata）。

存储调用经 `IoScheduler::dispatch`（`co_withExecutor`）到 Storage IO，完成后
`Task::await_transform` 把算子恢复到 Compute 池。

### 3.3 启动配置

`src/program/server/eugraph_server_main.cpp`：

```cpp
server->setIOThreadPool(ioPool);                      // 网络 IO + 流循环 + 序列化
server->setThreadManagerFromExecutor(ioPool.get());   // fbthrift handler 调度入口
// handler body 由 async_tm_* 自己调度到 GraphManager::computeExecutor()
```

`setThreadManagerFromExecutor(ioPool)` 保留：fbthrift 需要它在连接 EB 线程上调用
`async_tm_*`；它不是 handler body 的执行池。

### 3.4 仍未分离的部分（有意保留 / 后续可改）

下面这些 WT 相关动作**不经过 Storage IO 池**，仍然在语句当前的线程上同步执行。
它们是“会话所有权”设计的直接结果，而不是没改到的 inline dispatch：

1. **事务 begin / commit**：`AsyncGraphDataStore::beginSession/commitSession` 已把
   `GraphSession::begin_*`/`commit`（`txn_commit`，可能 fsync）经 `IoScheduler` 派发到
   Storage IO 池；Thrift 流结束时 commit 不再阻塞网络线程，Bolt 也不再阻塞 Compute。
   `StreamContext::commitAsync()` 是唯一提交入口。**rollback 在析构路径上不能 await**，
   仍同步执行（不 fsync，代价很小）；显式回滚路径同样保留同步。
2. **cursor close**：`phys_op`/generator 销毁时 `WtCursor` 析构在流的销毁线程（Thrift IO
   或 Bolt Compute）上执行，不走 Storage IO 池。close 通常很轻，但严格说仍是一处 WT 调用。
3. **stream 的第一段**：`co_executeCypherImpl` 在 async_tm 路径会先在 Compute 预取
   **第一个 batch**（`prefetch_first_chunk`），因此首段的同步算子工作不在 Thrift IO 上。
   代价是首包延迟到第一个 batch 就绪；后续 batch 仍用 `co_viaIfAsync`（只绑定 executor，
   不调度），每次从 yield 恢复后的第一段同步代码可能仍在 Thrift IO 上，直到下一个
   `co_await`。要连后续 batch 的同步段也严格放到 Compute，需要生产者/有界队列模型
   （或把 `next()` 包成 Task + `co_withExecutor`，但要处理流取消时协程帧脱离结构化的
   风险，见 §3.2）。
4. **图生命周期**：`createGraph` / `dropGraph` / open 连接 / checkpoint 等同步 WT
   管理操作在调用线程（Thrift handler 的 Compute、startup 主线程、checkpoint 线程）执行，
   不走 Storage IO 池；它们不是查询热路径。
5. **启动期**：`GraphManager::init` 的 `blockingWait` 打开 store/__anon__，只在启动时发生。

其余查询路径（所有 `AsyncGraphDataStore` 方法、游标创建/分批扫描、点/边读写、索引维护、
meta 读写）都经 `IoScheduler::dispatch` 到 Storage IO 池。

## 4. 测试

| 测试 | 覆盖 |
|------|------|
| `tests/test_rpc_integration.cpp::RequestResponseRpcHasNoMultiSecondStall` | 4 个网络 IO worker 下 20 次 `listLabels`，p50 < 1 s、max < 2 s（缺陷时 5–7 s） |
| `tests/test_rpc_integration.cpp::StreamingQueryCompletesWithoutStall` | 3000 行 `UNWIND` 流式查询 < 5 s，行数精确 |
| `tests/test_query_stream_hop.cpp`（4 个） | 包装 generator 的 operator 在 Compute 启动/恢复、consumer 仍在网络 IO executor、首 batch 预取、空流不重复 next |
| `tests/test_io_scheduler.cpp` | storage callable 在 IO executor、调用方回自己的 executor、异常传播 |
| TCK（`ctest -R tck_tests`） | 2026-10-31 实测 3959 scenarios / 3907 passed / 0 failed，769.2 s（旧基线约 1470–1500 s） |
| Bolt Python/Java/JS driver 集成 | 3/3 通过；覆盖显式 `BEGIN`/`COMMIT`/`ROLLBACK`（commit/begin 均经 Storage IO dispatch） |

`RpcIntegrationTest` 固定使用 4 个网络 IO worker（`setNumIOWorkerThreads(4)`），
因为 1 个 worker 会掩盖“恢复到不同 worker”的回归。

## 5. 相关文件

- `src/service/thrift/eugraph_handler.cpp` / `.hpp`
- `src/service/query_stream_hop.hpp`
- `src/service/graph_service.cpp`（`computeExecutor()`）
- `src/graph/graph_manager.hpp/.cpp`（进程共享 Compute 池）
- `docs/query/engine/execution-model.md` §三
- `docs/architecture/system-architecture.md`「线程模型」
