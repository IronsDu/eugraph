# RPC 服务层设计

> [当前实现] 参见 [README.md](README.md) 返回文档导航

EuGraph 使用 fbthrift 作为 RPC 框架，定义在 `proto/eugraph.thrift`。服务端 `EuGraphHandler` 实现 Thrift 接口，客户端 `EuGraphRpcClient` 封装连接与调用。

## 架构

```
┌──────────────────────┐               ┌──────────────────────────────────────┐
│    eugraph-shell     │               │         eugraph-server               │
│    (CLI REPL)        │               │                                      │
│                      │               │  EuGraphHandler                      │
│  :create-label  ─────┤               │  └─ DDL: createLabel / createEdgeLabel
│  :create-edge-l ─────┤── fbthrift ──►│     listLabels / listEdgeLabels
│  Cypher queries ─────┤    (TCP)      │  ├─ DML: executeCypher (流式)        │
│  :help / :exit       │               │  └─ Batch: batchInsertVertices/Edges │
│                      │               │                                      │
└──────────────────────┘               └──────────────────────────────────────┘
```

## Handler 方法

| 方法 | 实现 |
|------|------|
| `co_createLabel` | Thrift PropertyDef → 内部 PropertyDef，`async_meta_.createLabel()` → `async_data_.createLabel()` |
| `co_createEdgeLabel` | 同上，额外设置 `directed = true` |
| `co_listLabels` / `co_listEdgeLabels` | `async_meta_.listLabels/EdgeLabels()` → 转换为 Thrift 类型 |
| `co_executeCypher` | 接收可选 `parameters`（`map<string, string>`，值为 Cypher 字面值表达式如 `"42"`、`"'hello'"`），解析为 `Value` 后传给 `executor_.prepareStream()` → `StreamContext` → `makeStreamGenerator`（逐行 Value→Thrift 转换，流式 `co_yield ResultRowBatch`；流结束时 `commitTran`） |
| `co_batchInsertVertices` | 查 label_id → `async_meta_.nextVertexIdRange(count)` 批量分配 ID → `async_data_.batchInsertVertices()`（独立事务） |
| `co_batchInsertEdges` | 同上，`nextEdgeIdRange` + `batchInsertEdges` |

## 流式执行

`co_executeCypher` 返回 `ResponseAndServerStream<QueryStreamMeta, ResultRowBatch>`：

1. `prepareStream()` 返回 `shared_ptr<StreamContext>`（含物理算子树 + AsyncGenerator + 事务）
2. `makeStreamGenerator` 将 `AsyncGenerator<RowBatch>` 包装：逐批转换 Value → Thrift 类型，`co_yield ResultRowBatch`
3. 流正常结束时 `commitTran`；客户端断连则隐式回滚

详细见 [query/engine/execution-model.md](query/engine/execution-model.md)。

## 参数化查询

`executeCypher` 接受可选的 `parameters`（`map<string, string>`），用于参数化查询（`$param` 语法）：

```thrift
QueryStreamMeta,stream<ResultRowBatch> executeCypher(
  1: string query,
  2: string graph_name,
  3: map<string, string> parameters   // 参数名 → Cypher 字面值字符串
)
```

**参数值格式**：值为 Cypher 字面值表达式字符串，如 `"42"`（整数）、`"'hello'"`（字符串）、`"true"`（布尔）、`"3.14"`（浮点）。

**执行路径**：Handler 将参数字符串通过 CypherParser 解析为 `Value`，传给 `QueryExecutor::prepareStream()`，在 Binder 阶段 `$param` 被替换为 `BoundLiteral(value)`。优化器可直接看到字面值进行下推优化。

## Value → Thrift 转换

- `monostate` → null
- `bool/int64_t/double/string` → 对应 Thrift 字段
- `VertexValue` / `EdgeValue` / `PathValue` / `ListValue` → JSON string（对应 `vertex_json` / `edge_json` / `path_json` / `list_json` 字段）

## 结果序列化与 TCK 格式化

`thrift_fmt::formatResultValue`（`src/thrift_fmt/result_format.cpp`）将 Thrift `ResultValue` 转为 Cypher 语法字符串供 TCK 比较：

- `vertex_json` → `(:Label {prop: 'val'})`，`__anon__` 标签被过滤（内部默认标签，不可见）
- `edge_json` → `[:TYPE {prop: 'val'}]`
- `list_json` → 解析 JSON 数组，对每个元素递归格式化（边/顶点用上述规则），输出 `[elem1, elem2]`

`list_json` 由 handler 的 `valueToThrift` 构建：遍历 ListValue 元素，对每个元素递归调用 `valueToThrift` 后将原始 JSON 拼接。`formatResultValue` 的 `list_json` 分支负责将其解析回结构化形式并按 Cypher 语法输出。

## RPC 客户端

`EuGraphRpcClient` 管理独立的 `folly::EventBase` 后台线程（`loopForever()`）。

- DDL 调用使用 `semifuture_*().via(evb).get()` 模式（避免与 `loopForever()` 冲突）
- 查询使用 `sync_executeCypher` 获取 `ResponseAndClientBufferedStream`，`subscribeInline` 消费
- 初始化时 `evb_->runInEventBaseThreadAndWait()` 完成连接

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
  eugraph.thrift              # IDL 定义 (含 QueryStreamMeta, ResultRowBatch, batchInsert)
```
