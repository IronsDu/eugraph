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

## 待完成

### 0. 基础设施改进（P0-1 ~ P0-3，本次新增）
- [ ] SelectionVector null=identity
- [ ] Column CONSTANT 形态
- [ ] FunctionRegistry 代价模型

### 1. 物理算子升级到 DataChunk（Phase 7 — 核心改动）

需要改造的算子（按依赖顺序）：

| 算子 | 改动要点 |
|------|---------|
| `PhysicalOperator` 基类 | 新增 `executeChunk()` 虚函数，或修改 `execute()` 返回 `AsyncGenerator<DataChunk>` |
| `AllNodeScanPhysicalOp` | 按需获取属性（projection），输出 DataChunk |
| `LabelScanPhysicalOp` | 同 AllNodeScan |
| `IndexScanPhysicalOp` | 同 Scan |
| `EdgeIndexScanPhysicalOp` | 同 Scan |
| `ExpandPhysicalOp` | 批量获取邻居属性，边属性按需加载，输出 DataChunk |
| `FilterPhysicalOp` | 使用 VectorizedEvaluator + SelectionVector 替代 ExpressionEvaluator |
| `ProjectPhysicalOp` | 使用 VectorizedEvaluator 求值，列存追加 |
| `SortPhysicalOp` | 基于 Column 比较，预计算排序键 |
| `AggregatePhysicalOp` | 基于 Column 分组聚合，函数指针来自 FunctionRegistry |
| `DistinctPhysicalOp` | 基于 SelectionVector 标记重复行 |
| `SkipPhysicalOp` / `LimitPhysicalOp` | 基于计数调整 SelectionVector |
| `CreateNodeOp` / `CreateEdgeOp` | 适配新的属性传递格式 |
| `SetOp` / `RemoveOp` | 适配 BoundExpression 属性引用 |
| `PathBuildPhysicalOp` | 适配 DataChunk 列格式 |

### 2. RPC 层适配

- `EuGraphHandler` 中 `DataChunk → ResultRowBatch` 转换
- 或在最终输出前统一将 DataChunk 转为 RowBatch

### 3. 移除旧代码

- 删除 `ExpressionEvaluator`（被 `VectorizedEvaluator` 替代）
- 清理 `LogicalPlanBuilder` 中的字符串函数名匹配（迁移到 FunctionRegistry）

### 4. 测试

- 所有 392 个现有测试在新执行路径下通过
- 新增 Binder 单元测试（类型推断、错误检测）
- 新增 VectorizedEvaluator 单元测试
- 新增 DataChunk 操作测试

---

## 实施顺序（更新）

```
0.1 SelectionVector null=identity
    ↓
0.2 Column CONSTANT 形态
    ↓
0.3 FunctionRegistry 代价模型
    ↓
1. 升级 PhysicalOperator 基类接口
    ↓
2. 改造 Scan 算子（含投影下推）
    ↓
3. 改造 Filter / Project（核心计算）
    ↓
4. 改造 Expand
    ↓
5. 改造 Sort / Aggregate / Distinct
    ↓
6. 改造写算子
    ↓
7. RPC 适配
    ↓
8. 清理旧代码
    ↓
9. 测试
```
