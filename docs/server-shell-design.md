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
│  :create-edge-l ─────┤── fbthrift ──►│  │  EuGraphServiceHandler       │    │
│  :list-labels   ─────┤    (TCP)      │  │  (Thrift 请求路由)            │    │
│  Cypher queries ─────┤               │  └──────────┬───────────────────┘    │
│  :help / :exit       │               │             │                        │
│                      │               │    ┌────────▼────────┐              │
└──────────────────────┘               │    │ MetadataService │              │
                                       │    │ QueryExecutor   │              │
                                       │    │ GraphStore(WT)  │              │
                                       │    └─────────────────┘              │
                                       └──────────────────────────────────────┘
```

## 1. Thrift IDL 定义

文件：`proto/eugraph.thrift`

```thrift
namespace cpp2 eugraph.thrift

// ==================== Common Types ====================

enum PropertyType {
  BOOL = 1,
  INT64 = 2,
  DOUBLE = 3,
  STRING = 4,
  INT64_ARRAY = 5,
  DOUBLE_ARRAY = 6,
  STRING_ARRAY = 7,
}

struct PropertyDefThrift {
  1: i16 id,
  2: string name,
  3: PropertyType type,
  4: bool required = false,
}

union PropertyValueThrift {
  1: bool bool_val,
  2: i64 int_val,
  3: double double_val,
  4: string string_val,
  5: list<i64> int_array,
  6: list<double> double_array,
  7: list<string> string_array,
}

struct LabelInfo {
  1: i16 id,
  2: string name,
  3: list<PropertyDefThrift> properties,
}

struct EdgeLabelInfo {
  1: i16 id,
  2: string name,
  3: list<PropertyDefThrift> properties,
  4: bool directed = true,
}

// ==================== Query Result ====================

// Runtime value in query result
union ResultValue {
  1: bool bool_val,
  2: i64 int_val,
  3: double double_val,
  4: string string_val,
  // Vertex/Edge 以 JSON string 形式传递，shell 端直接显示
  5: string vertex_json,
  6: string edge_json,
}

struct ResultRow {
  1: list<ResultValue> values,
}

struct QueryResult {
  1: list<string> columns,       // 列名
  2: list<ResultRow> rows,       // 数据行
  3: i64 rows_affected,          // 写操作影响的行数
  4: string error,               // 错误信息（空字符串表示成功）
}

// ==================== Service ====================

service EuGraphService {
  // DDL: Label 管理
  LabelInfo createLabel(1: string name, 2: list<PropertyDefThrift> properties);
  LabelInfo getLabel(1: string name);
  list<LabelInfo> listLabels();

  // DDL: EdgeLabel 管理
  EdgeLabelInfo createEdgeLabel(1: string name, 2: list<PropertyDefThrift> properties);
  EdgeLabelInfo getEdgeLabel(1: string name);
  list<EdgeLabelInfo> listEdgeLabels();

  // DML: Cypher 查询
  QueryResult executeCypher(1: string query);
}
```

## 2. eugraph-server

文件位置：`src/server/`

### 组件

- `eugraph_server_main.cpp` — 入口，解析命令行参数，启动服务
- `eugraph_handler.h/cpp` — Thrift service handler，将 Thrift 请求路由到本地服务

### 启动流程

```
1. 解析命令行参数（--port, --data-dir）
2. 初始化 WiredTigerStore（打开数据目录）
3. 初始化 GraphStoreImpl
4. 初始化 MetadataServiceImpl（从 KV 加载元数据缓存）
5. 初始化 QueryExecutor（创建线程池）
6. 注册 Thrift handler，启动 fbthrift server
7. 等待 shutdown 信号
```

### 命令行参数

```
eugraph-server [--port 9090] [--data-dir ./data] [--threads 4]
```

### Handler 实现

`EuGraphHandler` 实现 thrift 生成的 `EuGraphServiceSvIf` 接口：

- `createLabel` → 调用 `MetadataServiceImpl::createLabel`
- `createEdgeLabel` → 调用 `MetadataServiceImpl::createEdgeLabel`
- `listLabels` → 调用 `MetadataServiceImpl::listLabels`
- `listEdgeLabels` → 调用 `MetadataServiceImpl::listEdgeLabels`
- `executeCypher` → 调用 `QueryExecutor::executeSync`，收集 RowBatch 转换为 QueryResult

### Value 转换

QueryExecutor 返回 `Row`（`vector<Value>`），其中 Value 是 variant 类型。
Handler 需要遍历 Row，将每个 Value 转换为 `ResultValue`（thrift union）：
- `monostate` → 不设置（null）
- `bool/int64_t/double/string` → 对应 thrift 字段
- `VertexValue` → 序列化为 JSON string，设置 vertex_json 字段
- `EdgeValue` → 序列化为 JSON string，设置 edge_json 字段

## 3. eugraph-shell

文件位置：`src/shell/`

### 组件

- `shell_main.cpp` — 入口，连接服务端，启动 REPL 循环
- `shell_repl.h/cpp` — REPL 逻辑（读取输入、解析命令、发送请求、格式化输出）

### REPL 设计

```
eugraph> :create-label Person name:STRING age:INT64
Label created: Person (id=1)

eugraph> :create-label Company name:STRING
Label created: Company (id=2)

eugraph> :create-edge-label KNOWS since:INT64
EdgeLabel created: KNOWS (id=1)

eugraph> :list-labels
 ┌────┬─────────┬──────────────────────┐
 │ ID │ Name    │ Properties           │
 ├────┼─────────┼──────────────────────┤
 │  1 │ Person  │ name:STRING, age:INT │
 │  2 │ Company │ name:STRING          │
 └────┴─────────┴──────────────────────┘

eugraph> CREATE (n:Person {name: "Alice", age: 30})
+-------------------+
| Created 1 vertex  |
| id: 1             |
+-------------------+

eugraph> MATCH (n:Person) WHERE n.name = "Alice" RETURN n.name, n.age
+------------+-------+
| n.name     | n.age |
+------------+-------+
| "Alice"    | 30    |
+------------+-------+
1 row returned (0.5ms)

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

Cypher 查询以 `;` 结尾。如果输入不以 `;` 结尾，进入多行模式，提示符变为 `......>`：

```
eugraph> MATCH (n:Person)
......> WHERE n.age > 25
......> RETURN n.name, n.age;
```

### 结果格式化

- **表格式**：有列名的查询结果，用 ASCII 表格显示
- **写操作**：显示影响的行数
- **DDL 操作**：显示确认信息
- **空结果**：显示 "(empty)" 或 "0 rows returned"

## 4. 构建集成

### 新增依赖

CMakeLists.txt 新增：
- `find_package(fbthrift CONFIG REQUIRED)`（vcpkg 安装）

### 构建目标

```
add_custom_command(
    OUTPUT ${THRIFT_GENERATED_SOURCES}
    COMMAND thrift1 --gen cpp2 -o ${OUTPUT_DIR} ${PROJECT_SOURCE_DIR}/proto/eugraph.thrift
    DEPENDS proto/eugraph.thrift
)

add_executable(eugraph-server ...)
add_executable(eugraph-shell ...)
```

### vcpkg 安装

```bash
vcpkg install fbthrift:x64-linux-static
```

## 5. 目录结构变更

```
eugraph/
├── proto/
│   └── eugraph.thrift              # 新增：Thrift IDL
├── src/
│   ├── server/                     # 新增：服务端
│   │   ├── eugraph_server_main.cpp
│   │   └── eugraph_handler.h/cpp
│   ├── shell/                      # 新增：Shell 客户端
│   │   ├── shell_main.cpp
│   │   └── shell_repl.h/cpp
│   └── ...（已有目录不变）
└── ...
```

## 6. 开发步骤

1. 安装 fbthrift 依赖（vcpkg）
2. 编写 Thrift IDL + 验证代码生成
3. 实现 eugraph-server（Handler + main）
4. 实现 eugraph-shell（REPL + Thrift client）
5. 端到端测试（手动走完整个流程）
