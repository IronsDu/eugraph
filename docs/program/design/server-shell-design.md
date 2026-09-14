# Server + Shell 设计

> [当前实现] 参见 [README.md](README.md) 返回文档导航

RPC 层与通信协议见 [service/rpc-service.md](../service/rpc-service.md)。

## Server 启动流程

1. 创建 SyncGraphDataStore（`{data_dir}/data`）+ SyncGraphMetaStore（`{data_dir}/meta`）
2. 创建共享 `IoScheduler(storage_io_threads=4)` 与共享 `compute_pool_(compute_threads=4)`
3. 创建 AsyncGraphDataStore + AsyncGraphMetaStore
4. 创建 QueryExecutor(async_data, async_meta, config{compute_pool = 共享池})
5. 创建 EuGraphHandler
6. Thrift server 的 `IOThreadPoolExecutor` 同时作为网络 IO 与 handler 执行池

`--compute-threads` 控制查询计算线程池；`--storage-io-threads` 控制存储 IO 池（`IoScheduler`），`--thrift-io-threads` 控制 Thrift IO/handler 池。

`IoScheduler` 池、Thrift IO 池与 Compute 池三者相互独立。Compute 池与 `IoScheduler` 一样由 `GraphManager` 持有、**全局唯一**，所有图共享。Thrift handler 必须在本线程（IO 池）上开始并结束，只把查询的计划/准备阶段外派到 Compute 池再跳回；把 handler 整体搬到 Compute 池会让请求-响应型 RPC 的回包延迟数秒。详见 [运行时执行模型](../../query/engine/execution-model.md)。

## Shell

Shell 通过 RPC 连接 server，查询结果通过 `subscribeInline` 流式打印。

## 文件结构

```
src/program/server/
  eugraph_server_main.cpp     # 服务入口
  eugraph_handler.hpp/cpp     # Thrift handler
src/program/shell/
  shell_main.cpp              # Shell 入口
  shell_repl.hpp/cpp          # REPL 逻辑
  rpc_client.hpp/cpp          # Thrift 客户端封装
proto/
  eugraph.thrift              # IDL 定义
```
