# fbthrift 服务层 + Shell 设计

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

## 架构

```
┌──────────────────────┐               ┌──────────────────────────────────────┐
│    eugraph-shell     │               │         eugraph-server               │
│    (CLI REPL)        │               │                                      │
│                      │               │  EuGraphHandler                      │
│  :create-label  ─────┤               │  ├─ DDL: createLabel / createEdgeLabel
│  :create-edge-l ─────┤── fbthrift ──►│  ├─ DML: executeCypher (流式)        │
│  Cypher queries ─────┤    (TCP)      │  ├─ Batch: batchInsertVertices/Edges │
│  :help / :exit       │               │  └─ Index: createIndex / dropIndex    │
│                      │               │                                      │
└──────────────────────┘               └──────────────────────────────────────┘
```

## 1. eugraph-server

### 启动参数

```
eugraph-server --port 9090 --data-dir ./eugraph-data --threads 4
```

`--threads` 控制 compute 线程数，IO 线程数固定为 4。

### 启动流程

1. 创建 SyncGraphDataStore（`{data_dir}/data`）+ SyncGraphMetaStore（`{data_dir}/meta`）
2. 创建共享 `IoScheduler(io_threads=4)`
3. 创建 AsyncGraphDataStore + AsyncGraphMetaStore
4. 创建 QueryExecutor(async_data, async_meta, config{compute_threads})
5. 创建 EuGraphHandler
6. Thrift server 使用 `IOThreadPoolExecutor` 同时作为 IO 和 handler 线程池

### Handler 方法

| 方法 | 实现 |
|------|------|
| `co_createLabel` | `async_meta_.createLabel()` → `async_data_.createLabel()` |
| `co_createEdgeLabel` | `async_meta_.createEdgeLabel()` → `async_data_.createEdgeLabel()` |
| `co_listLabels` / `co_listEdgeLabels` | 直接查 meta store |
| `co_executeCypher` | `executor_.prepareStream()` → `StreamContext` → `makeStreamGenerator` → `ServerStream<ResultRowBatch>` |
| `co_batchInsertVertices` / `co_batchInsertEdges` | 批量写入（独立事务，供 loader 使用） |

### 流式执行

`co_executeCypher` 返回 `ResponseAndServerStream<QueryStreamMeta, ResultRowBatch>`：

1. `prepareStream()` 返回 `shared_ptr<StreamContext>`（含物理算子树 + AsyncGenerator + 事务）
2. `makeStreamGenerator` 将 `AsyncGenerator<RowBatch>` 包装：逐批转换 Value → Thrift 类型，`co_yield ResultRowBatch`
3. 流正常结束时 `commitTran`；客户端断连则隐式回滚

详细见 [execution-model.md](execution-model.md)。

### Value → Thrift 转换

- `monostate` → null
- `bool/int64_t/double/string` → 对应 Thrift 字段
- `VertexValue` / `EdgeValue` → JSON string

## 2. eugraph-shell

### 双模式

| 模式 | 启动参数 | 说明 |
|------|----------|------|
| RPC 模式 | `--host ::1 --port 9090` | 连接远程 server，流式消费 `ClientBufferedStream` |
| 嵌入式模式 | `-d /path/to/data` | 本地直连存储层，绕过 RPC |

RPC 模式下查询结果通过 `subscribeInline` 流式打印；嵌入式模式下调用 `executor_.executeAsync()` 全量获取。

### Shell 命令

| 命令 | 说明 |
|------|------|
| `:create-label <name> <prop1>:<type1> ...` | 创建标签 |
| `:create-edge-label <name> <prop1>:<type1> ...` | 创建关系类型 |
| `:list-labels` / `:list-edge-labels` | 列出标签/关系类型 |
| `:help` / `:exit` / `:quit` | 帮助/退出 |
| 其他（不以 `:` 开头） | 作为 Cypher 查询发送，`;` 结尾 |

### 多行输入

不以 `;` 结尾时进入多行续写模式（`......> ` 提示符）。

## 3. RPC 客户端

`EuGraphRpcClient` 管理独立的 `folly::EventBase` 后台线程（`loopForever()`）。

- DDL 调用使用 `semifuture_*().via(evb).get()` 模式（避免与 `loopForever()` 冲突）
- 查询使用 `sync_executeCypher` 获取 `ResponseAndClientBufferedStream`，`subscribeInline` 消费
- 初始化时 `evb_->runInEventBaseThreadAndWait()` 完成连接

## 4. 文件结构

```
src/server/
  eugraph_server_main.cpp     # 服务入口
  eugraph_handler.hpp/cpp     # Thrift handler
src/shell/
  shell_main.cpp              # Shell 入口
  shell_repl.hpp/cpp          # REPL 逻辑 (embedded + RPC)
  rpc_client.hpp/cpp          # Thrift 客户端封装
proto/
  eugraph.thrift              # IDL 定义 (含 QueryStreamMeta, ResultRowBatch, batchInsert)
```
