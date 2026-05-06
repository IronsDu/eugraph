# Binder + Chunk 执行引擎改造进度

> **分支**: `feature/binder-chunk-executor`  
> **基准**: 392 tests passing (before changes)  
> **设计文档**: [binder-chunk-design.md](binder-chunk-design.md)

---

## 已完成

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

## 待完成

### 1. 编译错误修复

| 文件 | 问题 | 优先级 |
|------|------|--------|
| `plan_binder.cpp` | 格式化后 include 排序导致 BoundLiteral 等类型不完整（`bound_expression.hpp` 中 include 顺序被 linter 调整） | 高 |
| `binder.cpp` | AST `Expression` 类型为 `variant<unique_ptr<...>>` 不可拷贝，多处赋值/传递触发编译器错误 | 中 |

### 2. PlanBinder 集成修复

- **SEGFAULT 根因分析**：启用 PlanBinder 时多个测试 SEGFAULT，疑似 `walkAndBind` 递归中对 `unique_ptr` 生命周期处理问题
- **策略**：先以非侵入模式（warn-only）运行，逐步收紧到 error 级别

### 3. 物理算子升级到 DataChunk（Phase 7 — 核心改动）

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

### 4. RPC 层适配

- `EuGraphHandler` 中 `DataChunk → ResultRowBatch` 转换
- 或在最终输出前统一将 DataChunk 转为 RowBatch

### 5. 移除旧代码

- 删除 `ExpressionEvaluator`（被 `VectorizedEvaluator` 替代）
- 清理 `LogicalPlanBuilder` 中的字符串函数名匹配（迁移到 FunctionRegistry）

### 6. 测试

- 所有 392 个现有测试在新执行路径下通过
- 新增 Binder 单元测试（类型推断、错误检测）
- 新增 VectorizedEvaluator 单元测试
- 新增 DataChunk 操作测试

---

## 实施建议顺序

```
1. 修复编译错误 → 2. 修复 PlanBinder SEGFAULT → 3. 升级基类接口
→ 4. 改造 Scan 算子（含投影下推）→ 5. 改造 Filter/Project（核心计算）
→ 6. 改造 Expand → 7. 改造 Sort/Aggregate/Distinct
→ 8. 改造写算子 → 9. RPC 适配 → 10. 清理旧代码 → 11. 测试
```
