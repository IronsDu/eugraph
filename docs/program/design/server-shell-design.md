# Server + Shell 设计

> [当前实现] 参见 [README.md](README.md) 返回文档导航

RPC 层与通信协议见 [service/rpc-service.md](../service/rpc-service.md)。

## Server 启动流程

1. 创建 SyncGraphDataStore（`{data_dir}/data`）+ SyncGraphMetaStore（`{data_dir}/meta`）
2. 创建共享 `IoScheduler(io_threads=4)`
3. 创建 AsyncGraphDataStore + AsyncGraphMetaStore
4. 创建 QueryExecutor(async_data, async_meta, config{compute_threads})
5. 创建 EuGraphHandler
6. Thrift server 使用 `IOThreadPoolExecutor` 同时作为 IO 和 handler 线程池

`--threads` 控制 compute 线程数，IO 线程数固定为 4。

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
