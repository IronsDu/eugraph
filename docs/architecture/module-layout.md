# 模块布局与依赖规则

> 状态：生效（2026-11-17）。跟踪 issue：[#280](https://github.com/IronsDu/eugraph/issues/280)。
>
> 本文回答两个问题：**新文件放哪个目录**、**哪些 include 是禁止的**。
> 规则由 [`scripts/check-layering.sh`](../../scripts/check-layering.sh) 检查（<1s，无需构建）。
> 分层职责与线程模型的详细说明见 [system-architecture.md](system-architecture.md)。

---

## 1. 分层与依赖方向

依赖只能指向**更低**的层（同层内互相 include 允许）。这张表是当前代码树的实测结果，
不是设想：`check-layering.sh` 每次都会重新验证它。

| 层 | 目录 | 允许 include 的顶层目录 |
|---|---|---|
| L0 基础 | `src/common/` | `common` |
| L1 存储 | `src/storage/` | `common` `storage` |
| L2 计算 | `src/query/` | `common` `query` `storage` |
| L3 组合根 | `src/graph/` | `common` `graph` `query` `storage` |
| L4 服务 | `src/service/` | `common` `graph` `query` `service` `storage` |
| L5 入口 | `src/program/` | `common` `graph` `program` `service` |

**禁止边**（脚本会拦）：

- `storage/` ↛ `query/`、`graph/`、`service/`、`program/` —— 存储层不知道查询/组合根的存在；
- `query/` ↛ `service/`、`program/`、`graph/` —— 计算层只认 `IAsyncGraphDataStore` / `IAsyncGraphMetaStore` 接口；
- `common/` ↛ 任何其他顶层目录；
- 任何层 ↛ `program/`。

**第三方边界**：

- `<wiredtiger.h>` 只允许出现在 `src/storage/`（其他层通过 async 接口访问存储）；
- `folly` 不限层（协程、`FunctionRef`、`AsyncGenerator` 是所有层的公共基础设施）；
- `tests/` 不受上表约束（可以依赖任意层），但生产代码**不得**依赖 `tests/`。

## 2. 目录 → 编译目标

一个目录必须能回答"它编进哪个 target"。目前 10 个静态库（另有 1 个 INTERFACE 的 tcmalloc 适配）
全部定义在根 [CMakeLists.txt](../../CMakeLists.txt)。

| 目录 | 职责 | 编译目标 |
|---|---|---|
| `src/common/{types,config,thread,concurrency,schema}` | 零 IO 基础类型、配置、NUMA 线程、准入原语、内存 schema | 多数 header-only；`temporal_value.cpp`/`query_error.cpp` 在 `eugraph_query_engine` |
| `src/storage/wt_*.{hpp,cpp}`、`schema_ddl.*` | WT 表级原语、会话/游标、错误分类、单连接 DDL 序列 | `eugraph_lib` |
| `src/storage/kv/` | Key/Value/IndexKey 编解码 | `eugraph_lib` |
| `src/storage/data/` | 图数据 store：`SyncGraphDataStore` + `AsyncGraphDataStore` | `eugraph_lib`（sync）；async 为头文件内联实现 |
| `src/storage/meta/` | 元数据 store（sync + async）、`GraphSchema` 持久化 | `eugraph_lib` + `eugraph_metadata` |
| `src/storage/catalog/` | 全局图目录（独立 WT 数据库） | `eugraph_lib` |
| `src/storage/io_scheduler.hpp` | IO 线程池 / 协程调度 | header-only |
| `src/graph/` | 图实例组合根：`GraphInstance` / `GraphManager` / `GraphLease` | `eugraph_shell_lib`（⚠ 见 §6） |
| `src/query/parser/` | Cypher / DDL 文本 → AST | `eugraph_compute` |
| `src/query/planner/` | Binder、`BoundLogicalOperator`、`ColumnResolver`、逻辑计划 | `eugraph_query_engine` |
| `src/query/optimizer/` | Cascades（Memo / 规则 / 抽取） | `eugraph_query_engine` |
| `src/query/physical_plan/` | `PhysicalPlanner` + 物理算子 | `eugraph_query_engine` |
| `src/query/{evaluator,function,dataset}/` | 向量化求值、标量/聚合函数、`DataChunk` | `eugraph_query_engine` |
| `src/query/executor/` | `QueryExecutor` 编排 | `eugraph_query_engine` |
| `src/query/catalog/` | **内存**符号目录（binder/optimizer 的 label/property/index 查找） | `eugraph_query_engine` |
| `src/service/{bolt,thrift,web}/` | Neo4j Bolt、fbthrift、Web Console | `eugraph_bolt` / `eugraph_thrift_fmt` / `eugraph_web` |
| `src/program/{server,shell,loader}/` | 三个可执行入口 | `eugraph-server` / `eugraph-shell` / `eugraph-loader` |

> `storage/catalog/` 与 `query/catalog/` 不是一回事：前者是**持久化**的图目录（WT 库），
> 后者是**内存**中的符号索引。二者同名但分属两层，不要合并。

## 3. 新文件落位：按顺序问

1. **没有 IO、可单测的纯类型/纯函数？** → `common/`（按 `types` / `config` / `thread` / `concurrency` / `schema` 归类）
2. **直接调 WiredTiger API？** → `storage/wt_*`（原语）还是 `storage/{kv,meta,catalog}/`（领域编码/元数据/目录）
3. **管理 WT 资源生命周期（session / 事务 / cursor）？** → `storage/session/`（拆分后；当前在 `storage/data/`，见 §6）
4. **图数据的读写与扫描？** → `storage/data/`，且只放 store 接口与实现
5. **查询流水线的某一阶段？** → `query/{parser,planner,optimizer,physical_plan,executor,evaluator,function,dataset}/`
6. **进程级组合（多图、连接、生命周期）？** → `graph/`；**可执行入口** → `program/`
7. **都不符合？** 先在本文件加一节说明它属于哪层、为什么，再写代码——不要"先放一个地方再说"。

## 4. 三个反模式（都发生过）

### 4.1 "为了不形成反向依赖，就地下沉"

低层需要高层的能力时，正确做法是**把能力下沉到它该在的层，并删掉高层旧实现**；
只做前半步会留下两份代码。典型：索引维护。

- 下沉版：`storage/data/index_maintenance.hpp`（198 行，仅 async store 使用）
- 旧实现：`query/physical_plan/operator/{vertex,edge}_index_maintenance.hpp`（353 行，仍被 5 个写算子、8 处 include 使用）
- 两份代码有同名 `VertexIndexEntry` 和约 130–145 行近似重复块。

### 4.2 "一个目录装两种变化原因"

`storage/data/` 同时装了图数据 store（6 文件 4539 行）和 WT 资源生命周期
（`session_types` / `txn_controller` / `cursor_provider` / `graph_session` / `session_pool`，
5 文件 947 行）。后者已有 **7 个专属测试**，说明测试都承认它是独立单元，目录却不承认。

### 4.3 "文件靠 section 注释分隔"

`storage/data/sync_graph_data_store.cpp`：2525 行 / 74 个成员函数 / 20 个 `// ====` 分节。
分节是拆文件的信号，不是替代品。

## 5. 检查

```bash
./scripts/check-layering.sh      # CI 用的形式：有违规退出码 1
./scripts/check-layering.sh -v    # 打印每条允许边与扫描计数
```

脚本做三件事：

1. 扫 `src/**` 的跨模块 `#include`，按 §1 的表判定方向（排除 `parser/generated/`、`thrift/gen-cpp2/`）；
2. 断言 `<wiredtiger.h>` 不出现在 `src/storage/` 之外；
3. 检查白名单是否腐烂：登记在 `ALLOWLIST` 里的例外，如果违规已消失会告警要求删除。

确需临时例外时在脚本的 `ALLOWLIST` 登记 `文件:目标模块`，并在同行或上方注明 issue 号。
**⚠ 该脚本尚未接入 CI**（改 workflow 属于需要确认的操作）；建议挂到现有的 lint job 里，
与 `check-format.sh` 并列。

## 6. 已知偏差（随 #280 修）

| 偏差 | 现状 | 归属 |
|---|---|---|
| 索引维护双实现 | 见 §4.1 | #280（合并，净删约 200 行） |
| `storage/data/` 混装 store 与 session | 见 §4.2 | #280（`storage/session/`） |
| `storage/wt_error.hpp` 上向依赖 `storage/data/session_types.hpp` | 最底层依赖上层目录 | #280（`TxnErrorClass` 下沉） |
| `storage/data/async_graph_data_store.cpp` 未被任何 target 编译 | 仅 1 行 `#include`，孤儿 TU | #280（并入实现或删除） |
| `graph/graph_manager.cpp` 编进 `eugraph_shell_lib` | 目标名与目录不对应 | #280（CMake 分目录） |
| [system-architecture.md](system-architecture.md) 写"13 种物理算子"、`query/logical_plan/` | 实际 44 个算子、实际路径 `query/planner/logical_plan/` | 随 #280 同步 |
