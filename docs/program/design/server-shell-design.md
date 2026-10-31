# Server + Shell 设计

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

RPC 层与通信协议见 [service/rpc-service.md](../../service/rpc-service.md)。

## Server 启动流程

1. 创建 SyncGraphDataStore（`{data_dir}/data`）+ SyncGraphMetaStore（`{data_dir}/meta`）
2. 创建共享 `IoScheduler(storage_io_threads=4)`
3. 创建 AsyncGraphDataStore + AsyncGraphMetaStore
4. 创建 QueryExecutor(async_data, async_meta, config{compute_threads})
5. 创建 EuGraphHandler
6. Thrift server 使用 `IOThreadPoolExecutor` 同时作为 IO 和 handler 线程池

`--compute-threads` 控制查询计算线程数；`--storage-io-threads` 控制存储 IO 池（`IoScheduler`），`--thrift-io-threads` 控制 Thrift IO/handler 池。

注意第 2 步的 `IoScheduler` 池、第 6 步的 Thrift 池与进程共享 Compute 池是**三个独立对象**。Thrift / RPC 模式下网络 IO、fbthrift 流循环与序列化在 Thrift IO 池；全部 RPC handler body 经 `async_tm_*` 调度到 Compute 池，完成回调显式投回连接 EventBase（否则请求-响应回包会被 fbthrift WriteBatcher 延迟 5–7s）。存储调用再经 `IoScheduler` 跳到 Storage IO 池并回到 Compute。Bolt 模式下 session 处理直接被调度到 Compute 池。详见 [运行时执行模型](../../query/engine/execution-model.md)。

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
