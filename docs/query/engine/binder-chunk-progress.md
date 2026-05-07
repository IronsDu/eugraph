# Binder + Chunk 执行引擎改造进度

> **分支**: `feature/binder-chunk-executor`  
> **基准**: 392 tests passing (before changes)  
> **设计文档**: [binder-chunk-design.md](binder-chunk-design.md)  
> **参考文档**: [/mnt/f/code/duckdb/duckdb_query_engine_design.md](../../mnt/f/code/duckdb/duckdb_query_engine_design.md)

---

## 已完成

### Phase 0: 编译修复与 PlanBinder SEGFAULT 修复
- `bound_expression_fwd.hpp`: 引入 `BoundLiteral`/`BoundColumnRef`/`BoundParameter` 完整头文件，解决 `std::variant` 不完整类型编译错误
- `plan_binder.cpp`: 修复两处 use-after-move（PropertyAccess 中 `ot` 引用悬空、ProjectOp 中 `getBoundExprType` 在 move 后调用）
- PlanBinder 经验证在 enable 状态下 392 测试全过

### Phase 1: 基础设施
- `BoundType` 类型系统 (`src/compute_service/binder/bound_type.hpp`) — 支持 10 种类型，具有类型推断、合并、隐式转换
- `Catalog` 目录系统 (`src/compute_service/catalog/`) — 管理 Label/EdgeLabel/Property 查找
- `FunctionRegistry` 函数注册表 (`src/compute_service/function/`) — 内置标量函数 (id/nodes/relationships/length) 和聚合函数 (count/sum/avg/min/max)，每个函数独立文件
- `FunctionDef` 函数描述 (`src/compute_service/function/function_def.hpp`) — 支持重载解析

### Phase 2: BoundExpression 类型
- 13 种 BoundExpression 类型，每种独立文件 (`src/compute_service/binder/bound_expression/`)
- `BoundExpression` variant 定义 + 访问器 + `getBoundExprType()`

### Phase 3: Binder
- `Binder` 类 (`src/compute_service/binder/binder.hpp/.cpp`) — 将 AST 绑定为 BoundStatement，支持多数子句绑定
- `PlanBinder` 后处理验证通过 (`src/compute_service/binder/plan_binder.hpp/.cpp`) — 在现有 LogicalPlan 上做表达式验证和类型收集
- 当前 PlanBinder **默认关闭**（通过 `Config::enable_binder` 控制）

### Phase 4: Bound Logical Plan
- `BoundLogicalPlan` + 15 种 BoundLogicalOperator 类型 (`src/compute_service/binder/bound_logical_plan.hpp`)

### Phase 5: DataChunk
- `DataChunk` + `Column` + `SelectionVector` (`src/compute_service/executor/data_chunk.hpp`) — 列存数据格式，支持 validity bitmap

### Phase 6: VectorizedEvaluator
- `VectorizedEvaluator` (`src/compute_service/executor/vectorized_evaluator.hpp/.cpp`) — 基于 BoundExpression 的求值器

### Phase 8: 存储接口
- `IAsyncGraphDataStore` 增加投影感知接口（带 projection 参数的 `getVertexProperties`/`getEdgeProperties`，`batchGetVertexProperties`），均有 default 实现向后兼容

### 集成
- `QueryExecutor` 集成 Catalog + FunctionRegistry 加载，PlanBinder 通过 feature flag 可选启用

---

## DuckDB 设计评审后新增 (2026-05-07)

> 阅读 [duckdb_query_engine_design.md](../../mnt/f/code/duckdb/duckdb_query_engine_design.md) 后，对比发现三个基础设施改进点，在物理算子升级前完成。

### P0-1: SelectionVector null=identity 优化

**动机**: DuckDB 中 `sel_vector == nullptr` 表示恒等选择（所有行有效），无需分配和填充 `[0,1,2,...]`。大部分查询不需要过滤，可以节省 1024×4=4KB 分配和初始化。

**设计**: 使用 `bool is_identity` 标记，而非裸指针。`operator[]` 在 identity 模式下直接返回索引值。

```cpp
struct SelectionVector {
    std::vector<uint32_t> indices;  // 仅在 !is_identity 时使用
    size_t count = 0;
    bool is_identity = true;
};
```

### P0-2: Column 增加 CONSTANT 形态

**动机**: 字面量（如 `42`、`"hello"`）在当前设计中需要在每行存储一份拷贝。DuckDB 的 `CONSTANT_VECTOR` 只存一个值 + count，所有行共享。对 Filter 后的常量列、字面量引用、`LIMIT` 等场景都有用。

**设计**: Column 增加 `VectorForm` 枚举，`CONSTANT` 形态下 `getValue(i)` 对任意 i 返回同一个值，不依赖 flat 数据数组。

```cpp
enum class VectorForm : uint8_t { FLAT, CONSTANT };

struct Column {
    VectorForm form = VectorForm::FLAT;
    Value constant_value;  // CONSTANT 形态下的共享值
    // ... existing flat data vectors unchanged
};
```

- `DICTIONARY` 形态暂不引入 Column 级别，后续在 DataChunk 级别以 slice/shared_ptr 方式实现零拷贝过滤。
- 避免使用裸指针：CONSTANT 用 `Value` 值存储（非指针），slice 场景预留 `std::shared_ptr<DataChunk>` 方案。

### P0-3: FunctionRegistry 基于代价的重载解析

**动机**: 当前 `FunctionDef::matches()` 只做布尔判断（`canCastTo`），当多个重载都匹配时无法择优。DuckDB 使用数值代价（`CastCost`），选择总代价最低的重载。

例如 `add(INT64, INT64)` 同时匹配 `add(INT64, INT64)`（代价 0）和 `add(DOUBLE, DOUBLE)`（代价 = INT64→DOUBLE 转换代价），应选前者。

**设计**:
- `BoundType` 增加 `implicitCastCost(const BoundType& target)` 方法，返回整数代价（-1 = 不可转换）
- 代价规则：完全匹配=0、INT64→DOUBLE=1、NULL→任意=1、ANY→任意=1、其他=不可转换
- `FunctionRegistry::lookup()`：对所有匹配重载计算参数总代价，选代价最小的

---

## Phase 7 完成状态 (2026-05-07)

### ✅ 已完成

#### 基础设施 (P0-1 ~ P0-3)
- [x] SelectionVector null=identity 优化
- [x] Column CONSTANT 形态
- [x] FunctionRegistry 基于代价的重载解析

#### PhysicalOperator 基类升级
- [x] 新增 `executeChunk()` 虚函数返回 `AsyncGenerator<DataChunk>`
- [x] `executeViaChunk()` 桥接: `executeChunk()` → RowBatch
- [x] 转换工具: `rowBatchToDataChunk()` / `dataChunkToRowBatch()`

#### Leaf Scan 算子 (Phase 7b)
- [x] `AllNodeScanPhysicalOp` — FLAT columns, `execute()` → `executeViaChunk()`
- [x] `LabelScanPhysicalOp` — 同上
- [x] `IndexScanPhysicalOp` — 同上
- [x] `EdgeIndexScanPhysicalOp` — DICTIONARY columns for src/dst/edge

#### Pass-Through 算子 (Phase 7c) — DICTIONARY 零拷贝
- [x] `FilterPhysicalOp` — DICTIONARY 共享 child buffer + 过滤后 dict_sel
- [x] `SkipPhysicalOp` — DICTIONARY 调整 dict_sel
- [x] `LimitPhysicalOp` — DICTIONARY 截断 dict_sel
- [x] `DistinctPhysicalOp` — DICTIONARY hash set 去重
- [x] `ExpandPhysicalOp` — 输入列 DICTIONARY + 新列 FLAT
- [x] `SetPhysicalOp` / `RemovePhysicalOp` — child 原样 yield
- [x] `PathBuildPhysicalOp` — 复制 child 列 + 新建 PATH FLAT 列

#### 表达式绑定 (在 PhysicalPlanner 中)
- [x] `planFilter()` / `planProject()` 使用 PlanBinder 将 `cypher::Expression` → `BoundExpression`
- [x] Filter/Project 算子构造函数接收 `BoundExpression`（非 `cypher::Expression`）
- [x] 算子使用 `VectorizedEvaluator` 替代 `ExpressionEvaluator`
- [x] PlanBinder 支持 Label-scoped property access（`n::Label.prop`）
- [x] PlanBinder 支持跨标签弱类型 property access（`n.prop` → 多候选）
- [x] Variable 绑定使用 `ColumnInfo::column_index` 直接索引（修复 unordered_map 迭代顺序问题）

#### Bug 修复
- [x] `VectorizedEvaluator::temp_columns_` deque 化避免 reallocation 后指针悬空 (heap-use-after-free)
- [x] `tryIndexScan` 中 `output_types` 被 move 两次导致 PlanOperatorResult 收到空 vector
- [x] `bind_context.hpp` 中 `ColumnInfo` 聚合初始化需要显式 `column_index = 0`

#### RPC 适配
- [x] `eugraph_handler.cpp`: `makeStreamGenerator` 消费 `DataChunk`，调用 `chunk->toRows()`
- [x] `query_executor.cpp`: `StreamContext::gen` 类型从 `AsyncGenerator<RowBatch>` 改为 `AsyncGenerator<DataChunk>`
- [x] DDL/EXPLAIN 路径用 `wrapRowBatchToChunkGenerator()` 包装

### ❌ 待完成

#### Phase 7d: Materializing Operators (未开始)
- [ ] `SortPhysicalOp` — 物化所有输入 chunk → 排序 → 重建 FLAT DataChunk
- [ ] `AggregatePhysicalOp` — 物化所有输入 → group by → 重建 FLAT DataChunk

#### Phase 7e: Write Operators (未开始)
- [ ] `CreateNodePhysicalOp` / `CreateEdgePhysicalOp` — drain child → store 变更 → 单行 FLAT DataChunk

#### Phase 7f: 遗留边界问题
- [ ] Path 相关测试 (5 个): `ReturnNamedPathSingleHop`, `ReturnNamedPathTwoHop`, `PathNodes`, `PathRelationships`, `PathLength` — `pv.elements.size() == 0`
- [ ] Multi-label 测试 (1 个): `PropertyConflictToList` — 期望 ListValue 但得到单个 string
- [ ] Old code cleanup: 移除 `ExpressionEvaluator`，清理旧的 `cypher::Expression` 引用

#### 测试状态
- **370/376** core tests pass (98%)
- 6 failures: 5 path-related + 1 multi-label property conflict

---

## 旧实施计划（供参考）
