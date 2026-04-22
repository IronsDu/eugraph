# fbthrift 服务层 + eugraph-shell 设计

## 目标

让用户可以通过交互式 Shell 手动体验 EuGraph 的完整流程：
DDL（创建标签/关系类型）→ DML（写入点/边）→ 查询（邻接、过滤等）

## 架构

```
┌──────────────────────┐               ┌──────────────────────────────────────┐
│    eugraph-shell     │               │         eugraph-server               │
│    (CLI REPL)        │               │         (全功能单机节点)              │
│                      │               │                                      │
│  :create-label  ─────┤               │  ┌──────────────────────────────┐    │
│  :create-edge-l ─────┤── fbthrift ──►│  │  EuGraphHandler              │    │
│  :list-labels   ─────┤    (TCP)      │  │  (依赖 async 接口)            │    │
│  Cypher queries ─────┤               │  └──────────┬───────────────────┘    │
│  :help / :exit       │               │             │                        │
│                      │               │    ┌────────▼────────┐              │
└──────────────────────┘               │    │ IAsyncGraphMeta  │              │
                                       │    │ IAsyncGraphData  │              │
                                       │    │ QueryExecutor    │              │
                                       │    └─────────────────┘              │
                                       └──────────────────────────────────────┘
```

## 1. eugraph-server

文件位置：`src/server/`

### 组件

- `eugraph_server_main.cpp` — 入口，解析命令行参数，初始化存储层，启动服务
- `eugraph_handler.hpp/cpp` — Thrift handler，将 Thrift 请求路由到 async 接口

### 启动流程

```
1. 解析命令行参数（--port, --data-dir, --threads）
2. 创建 SyncGraphDataStore，open({data_dir}/data)
3. 创建 SyncGraphMetaStore，open({data_dir}/meta)
4. 创建共享 IoScheduler(io_threads)
5. 创建 AsyncGraphDataStore(sync_data, io_scheduler)
6. 创建 AsyncGraphMetaStore，open(sync_meta, io_scheduler)
7. 创建 QueryExecutor(async_data, async_meta, config)
8. 创建 EuGraphHandler(async_data, async_meta, executor)
9. 启动 fbthrift server
```

### 命令行参数

```
eugraph-server [--port 9090] [--data-dir ./eugraph-data] [--threads 4]
```

### Handler 实现

`EuGraphHandler` 依赖 `IAsyncGraphDataStore` 和 `IAsyncGraphMetaStore`：

```cpp
EuGraphHandler(IAsyncGraphDataStore& async_data,
               IAsyncGraphMetaStore& async_meta,
               compute::QueryExecutor& executor);
```

- `co_createLabel` → `co_await async_meta_.createLabel()` + `co_await async_data_.createLabel()`
- `co_createEdgeLabel` → `co_await async_meta_.createEdgeLabel()` + `co_await async_data_.createEdgeLabel()`
- `co_listLabels` → `co_await async_meta_.listLabels()`
- `co_listEdgeLabels` → `co_await async_meta_.listEdgeLabels()`
- `co_executeCypher` → `executor_.executeAsync()`，结果转 Thrift QueryResult

### Value 转换

QueryExecutor 返回 `Row`（`vector<Value>`），Handler 遍历 Row 转换为 `ResultValue`：
- `monostate` → null
- `bool/int64_t/double/string` → 对应 thrift 字段
- `VertexValue` → JSON string (vertex_json)
- `EdgeValue` → JSON string (edge_json)

## 2. eugraph-shell

文件位置：`src/shell/`

### 双模式

Shell 支持两种运行模式：

| 模式 | 启动参数 | 说明 |
|------|----------|------|
| RPC 模式 | `--host ::1 --port 9090` | 连接远程 server |
| 嵌入式模式 | `-d /path/to/data` | 本地嵌入式，不需要 server |

### REPL 设计

```
eugraph> :create-label Person name:STRING age:INT64
Label created: Person (id=1)

eugraph> :create-edge-label KNOWS since:INT64
EdgeLabel created: KNOWS (id=1)

eugraph> :list-labels
+----+--------+---------------------+
| ID | Name   | Properties          |
+----+--------+---------------------+
|  1 | Person | name, age           |
+----+--------+---------------------+

eugraph> CREATE (alice:Person {name: "Alice", age: 30});
OK

eugraph> MATCH (n:Person) RETURN n.name, n.age;
+---------+-------+
| n.name  | n.age |
+---------+-------+
| "Alice" | 30    |
+---------+-------+
1 rows

eugraph> :exit
Bye!
```

### Shell 命令

| 命令 | 说明 | Thrift API |
|------|------|-----------|
| `:create-label <name> <prop1>:<type1> ...` | 创建标签 | `createLabel` |
| `:create-edge-label <name> <prop1>:<type1> ...` | 创建关系类型 | `createEdgeLabel` |
| `:list-labels` | 列出所有标签 | `listLabels` |
| `:list-edge-labels` | 列出所有关系类型 | `listEdgeLabels` |
| `:help` | 显示帮助 | 本地 |
| `:exit` / `:quit` | 退出 Shell | 本地 |
| 其他（不以 `:` 开头） | 作为 Cypher 查询发送 | `executeCypher` |

### 多行输入

Cypher 查询以 `;` 结尾。如果输入不以 `;` 结尾，进入多行模式：

```
eugraph> MATCH (n:Person)
......> WHERE n.age > 25
......> RETURN n.name, n.age;
```

## 3. 目录结构

```
src/server/
├── eugraph_server_main.cpp     # 服务入口
└── eugraph_handler.hpp/cpp     # Thrift handler

src/shell/
├── shell_main.cpp              # Shell 入口
├── shell_repl.hpp/cpp          # REPL 逻辑 (embedded + RPC)
└── rpc_client.hpp/cpp          # Thrift 客户端
```
