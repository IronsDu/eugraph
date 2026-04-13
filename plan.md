# EuGraph 开发计划

## 项目目标

实现一个分布式图数据库，支持 Cypher 查询语言。

---

## 当前阶段：阶段一 基础框架 (MVP) — 进行中

**设计文档**：参见 [docs/overview.md](docs/overview.md)

| 主题 | 文档 |
|------|------|
| 系统架构 | [docs/system-architecture.md](docs/system-architecture.md) |
| 数据模型 | [docs/data-model.md](docs/data-model.md) |
| KV 编码 | [docs/kv-encoding.md](docs/kv-encoding.md) |
| 类型定义 | [docs/type-definitions.md](docs/type-definitions.md) |
| 接口设计 | [docs/interfaces.md](docs/interfaces.md) |
| 目录结构 | [docs/directory-structure.md](docs/directory-structure.md) |
| 开发阶段 | [docs/development-phases.md](docs/development-phases.md) |

### 任务进度

| # | 任务 | 状态 |
|---|------|------|
| 1.1 | 项目骨架搭建（CMake、目录结构、基础类型） | ✅ 已完成 |
| 1.2 | KV 存储引擎集成（IKVEngine、RocksDBStore、KeyCodec、ValueCodec） | ✅ 已完成 |
| 1.3 | 图存储层（IGraphStore、GraphStoreImpl、Vertex/Edge CRUD） | ✅ 已完成 |
| 1.4 | WiredTiger 存储引擎集成（WiredTigerStore、IKVEngine 兼容验证） | ✅ 已完成 |
| 1.4a | 存储层重构：去除 IKVEngine，WiredTiger 多表架构，DDL 支持 | ✅ 已完成 |
| 1.5 | Cypher Parser + AST（ANTLR4 语法、AST 节点定义、32 测试通过） | ✅ 已完成 |
| 1.6 | 逻辑计划（AST → LogicalPlan 翻译，8 种算子，~25 测试） | ✅ 已完成 |
| 1.7 | 物理计划 + 协程基础设施（folly coroutines、IO 线程池、AsyncGraphStore） | ✅ 已完成 |
| 1.8 | 基础读执行器（Pull-based 管道、AsyncGenerator<RowBatch>） | ✅ 已完成 |
| 1.9 | 写操作 + QueryExecutor 组装（CREATE 支持，~40 端到端测试通过） | ✅ 已完成 |
| 1.10 | 元数据服务（Label/EdgeLabel 管理、ID 分配） | 待开始 |
| 1.11 | 基础事务支持（MVCC） | 待开始 |

### 已完成的工作

#### 1.1 项目骨架搭建 ✅
- CMakeLists.txt 配置（vcpkg 依赖管理：rocksdb, gtest, spdlog, lz4, zlib, zstd）
- src/ 目录结构创建
- 基础类型定义（graph_types.hpp, error.hpp, constants.hpp）

#### 1.2 KV 存储引擎集成 ✅
- IKVEngine 抽象接口（put/get/del、prefix scan、事务）
- RocksDBStore 实现（基于 RocksDB 的 IKVEngine 实现）
- KeyCodec（各类型 Key 的编解码：L/I/P/R/X/E/D 前缀）
- ValueCodec（PropertyValue 的序列化/反序列化，支持所有类型及压缩）
- 单元测试（test_key_codec.cpp, test_value_codec.cpp）

#### 1.3 图存储层 ✅
- IGraphStore 抽象接口（Vertex/Edge CRUD、标签管理、边遍历、主键查询、统计）
- GraphStoreImpl 实现（基于 IKVEngine 的图语义存储）
- 支持功能：
  - 顶点增删、属性读写（单个/批量）、标签增删
  - 边增删、属性读写、双向索引
  - 主键正反向查询
  - 按标签扫描顶点、边遍历
  - 事务支持（提交/回滚）
- 单元测试（test_graph_store.cpp）
- **设计决策**：`insertVertex` 允许不指定标签（后续可通过 `addVertexLabel` + `putVertexProperties` 补充）

#### 1.4 WiredTiger 存储引擎集成 ✅
- WiredTiger (mongodb-8.2.7) 作为 git submodule 引入 `third_party/wiredtiger`
- 通过 ExternalProject_Add 独立构建（规避 WT 内部 CMAKE_SOURCE_DIR 硬编码问题）
- GCC 15 兼容性通过 `-Wno-error=array-bounds` 解决
- WiredTigerStore 实现 IKVEngine 全部接口：
  - open/close、put/get/del、putBatch、prefixScan、createScanCursor
  - 事务支持（begin/commit/rollback，snapshot isolation）
- 使用 `key_format=u,value_format=u` 存储 raw bytes
- 验证测试：
  - test_wiredtiger.cpp：WiredTiger 原生 API 验证（7 项测试，6 通过）
  - test_graph_store_wt.cpp：WiredTiger 后端 GraphStoreImpl 兼容性测试（8 项测试，全部通过）
- **设计决策**：引入 WiredTiger 作为可选存储引擎，为后续 MVCC 实现提供引擎级快照隔离能力

#### 1.5 Cypher Parser + AST ✅

- **设计文档**：[docs/cypher-parser-design.md](docs/cypher-parser-design.md)
- ANTLR4 语法（从 grammars-v4/cypher 获取，BSD 许可证）预生成 C++ 代码提交到仓库
- 自定义 AstBuilder 直接遍历 ANTLR Parse Tree（不继承 BaseVisitor，避免 std::any 与 move-only 类型冲突）
- AST 节点定义（`ast.hpp`）：Expression（19 种）、Clause（10 种）、Pattern 类型、Statement
- CypherQueryParser 类（`cypher_parser.hpp/cpp`）：`parse(string) → variant<Statement, ParseError>`
- 32 个单元测试全部通过（解析正确性、AST 结构验证、错误报告、边界情况）
- 分支：`feat/cypher-parser-m1`

#### 1.6 逻辑计划 ✅

- **设计文档**：[docs/query-engine-design.md](docs/query-engine-design.md)
- 8 种逻辑算子：AllNodeScan, LabelScan, Expand, Filter, Project, Limit, CreateNode, CreateEdge
- 使用 `variant<unique_ptr<XxxOp>>` 模式（与 AST 一致）
- 保持字符串名称，name→ID 解析推迟到物理计划阶段
- 符号表跟踪变量→列索引映射
- ~25 个测试覆盖所有算子类型和翻译路径

#### 1.7 物理计划 + 协程基础设施 ✅

- **设计文档**：[docs/query-engine-design.md](docs/query-engine-design.md)
- 物理算子与逻辑算子一一对应，增加 `AsyncGraphStore&` 引用和解析后的 ID
- `IoScheduler`：封装 folly IOThreadPoolExecutor，提供 `dispatch` / `dispatchVoid` 方法
- `AsyncGraphStore`：IGraphStore 的异步包装，所有存储调用通过 IO 线程池异步执行
- `PlanContext`：携带 label/edge_label name→ID 映射、预分配的 VertexId/EdgeId
- Pull-based 火山模型，`AsyncGenerator<RowBatch>`（批量 1024 行）

#### 1.8 基础读执行器 ✅

- 已实现算子：AllNodeScan, LabelScan, Expand, Filter, Project, Limit
- `ExpressionEvaluator`：运行时表达式求值（Literal, Variable, BinaryOp, UnaryOp, PropertyAccess, FunctionCall）
- 协程管道：Compute 线程执行逻辑运算，IO 请求通过 `co_await` 异步调度
- 5 个端到端测试通过（WiredTiger 后端）

#### 1.9 写操作 + QueryExecutor 组装 ✅

- 已实现算子：CreateNode, CreateEdge
- `QueryExecutor`：编排 parse → logical plan → physical plan → execute 完流程
- 事务管理：每次查询在独立事务中执行
- `executeSync`：通过 `co_withExecutor(compute_pool_)` 确保协程链在 compute executor 上启动
- 端到端测试覆盖（~40 个测试用例）：
  - LabelScan / AllNodeScan：多标签扫描、空标签、独立扫描
  - Expand：出边展开、无边空结果、多边展开、WHERE 过滤、LIMIT
  - WHERE 过滤：true/false 字面量、AND/OR/NOT 逻辑运算、组合表达式
  - CREATE：创建顶点返回 ID、连续创建、不同标签、创建边验证、创建后展开
  - LIMIT：边界情况（0、1、等于行数、大于行数）
  - RETURN *：全列返回、带 LIMIT
  - 组合场景：创建后扫描过滤、空图全操作、多标签混合

#### 查询引擎流水线

```
Cypher → ANTLR Parser → AST → LogicalPlanBuilder → LogicalPlan
  → [Optimizer 占位] → PhysicalPlanner → PhysicalPlan → QueryExecutor → AsyncGenerator<Row>
```

### 构建方式
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/mnt/f/code/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
./build/eugraph_tests          # RocksDB 后端测试
./build/wt_tests               # WiredTiger 原生 API 测试
./build/cypher_parser_tests    # Cypher 解析器测试（32 个）
./build/logical_plan_tests     # 逻辑计划测试（待实现）
```

---

## 后续阶段（待规划）

### 阶段三：查询能力（进行中）
- Cypher 解析器（ANTLR4）✅
- 逻辑计划（进行中）
- 物理计划 + 协程执行器（待开始）

### 阶段四：分布式扩展
- RPC 通信（fbthrift）
- 分布式事务（2PC）

---

## 设计调整记录

此处记录对设计文档的修改和调整决策。

| 日期 | 修改内容 | 原因 |
|------|----------|------|
| 2025-03-31 | 属性存储（X\|）Key 改用 prop_id 代替 property_name | 支持部分属性查询，字段改名无需修改数据存储 |
| 2025-03-31 | Edge 数据（D\|）改为 D\|{edge_label_id}\|{edge_id}\|{prop_id}，属性分开存储 | 支持部分属性查询，支持遍历某关系类型下所有边 |
| 2025-03-31 | 元数据 properties 改为数组，包含 prop_id 映射 | 支持属性名与 prop_id 的双向查找 |
| 2025-03-31 | 内存结构 Properties 改为按 prop_id 索引的数组 | 与 KV 存储设计一致，内存更紧凑，访问更快 |
| 2025-03-31 | Edge 数据（D\|）改为 D\|{edge_label_id}\|{edge_id}\|{prop_id}，属性分开存储 | 与 Vertex 属性存储（X\|）设计一致，支持部分属性查询 |
| 2025-03-31 | 主键明确定义为全局唯一标识，每顶点一个 | 主键是顶点的全局标识，不属于任何标签 |
| 2025-03-31 | 新增主键反向索引 R\|{vertex_id} → {pk_value} | 支持从 vertex_id 反查全局主键 |
| 2025-03-31 | 移除 LabelDef 中的 primary_key_prop_id 字段 | 标签内唯一约束由 IndexDef(unique=true) 表达 |
| 2026-04-01 | KV 存储引擎从 WiredTiger 切换为 RocksDB | WiredTiger 在 GCC 15 下存在 DT_INIT 链接问题，RocksDB 更成熟且 vcpkg 支持更好 |
| 2026-04-01 | 引入 IKVEngine 抽象接口 + IGraphStore 图语义层 | 解耦存储引擎与图操作，支持后续替换底层引擎 |
| 2026-04-01 | 新增 putVertexProperties 批量属性设置接口 | 一次写入某标签下所有属性，减少多次单独调用 |
| 2026-04-01 | insertVertex 允许不指定标签 | 支持先创建顶点再逐步补充标签和属性的场景 |
| 2026-04-10 | 查询语言从 GSQL 切换为 Cypher（OpenCypher） | Cypher 生态更成熟，ANTLR4 语法可直接复用 |
| 2026-04-10 | 采用 ANTLR4 预生成 C++ 代码（不依赖 Java 构建） | 避免构建复杂性，预生成代码提交到仓库 |
| 2026-04-10 | AstBuilder 直接遍历 Parse Tree（不继承 BaseVisitor） | ANTLR BaseVisitor 返回 std::any，与 move-only AST 类型不兼容 |

---

## 待开发需求列表

（按优先级排列，逐个对齐设计与实现方案后开始开发）

）

以下需求按优先级排列，逐个对齐设计与实现方案后开始开发。

### A. MVCC（多版本并发控制）

| # | 需求 | 状态 |
|---|------|------|
| A.1 | 为数据加上版本，查询时获取当前事务可见的最新版本 | 待对齐 |
| A.2 | 保证写入多条数据的原子性 | 待对齐 |

### B. DDL（数据定义语言）

| # | 需求 | 说明 | 状态 |
|---|------|------|------|
| B.1 | 删除图 | 要求速度快，[设计文档](docs/multi-graph.md) | 设计完成 |
| B.2 | 删除某个标签类型 | 要求速度快，[设计文档](docs/ddl.md#一删除标签类型drop-label) | 设计完成 |
| B.3 | 删除某个关系类型 | 需同时删除该类型的所有边，[设计文档](docs/ddl.md#二删除关系类型drop-edgelabel) | 设计完成 |
| B.4 | 删除某个标签下的某个列 | [设计文档](docs/ddl.md#三列操作column-ddl) | 设计完成 |
| B.5 | 删除某个关系类型下的某个列 | 同 B.4 | 设计完成 |
| B.6 | 重命名某个标签类型下的某个列 | 纯元数据操作，见 DDL 文档 | 设计完成 |
| B.7 | 重命名关系类型下的某个列 | 纯元数据操作，见 DDL 文档 | 设计完成 |
| B.8 | 添加列 | 需指定默认值或允许为空，避免回填数据，见 DDL 文档 | 设计完成 |

---

## 待讨论问题

此处记录需要进一步讨论或确认的设计问题。

1. （待补充）
