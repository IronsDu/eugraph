# Evaluator 临时对象复用与分配优化设计

> **状态**：已落地第一批（P0/P1），后续项见 §8。
> **日期**：2026-11-03。
> **范围**：`ExpressionEvaluator` 及其在物理算子中的生命周期；不改 KV 布局、不加二级索引、不做
> intra-query 并行、不写手写 SIMD。
> **动机**：查询执行已经有 `DataChunk` / `ColumnBuffer` / `SelectionVector` 的列式外壳，但
> 表达式路径仍有 per-call、per-row、per-element 的临时对象构造；在 compute-heavy 查询上
> 这些分配会放大成可观的 CPU/allocator 成本。

---

## 1. 现象与证据

| 位置 | 问题 |
|---|---|
| [expression_evaluator.cpp](../../../src/query/evaluator/expression_evaluator.cpp) `evaluate` | 结果列非同一列时逐行 `getValue`/`setValue`，即使两边类型相同；已有 `Column::copyValueFrom` 未使用 |
| `evaluatePredicate` | 逐行构造 `Value` 再判 bool；BOOL 列可直接读 `bool_data` |
| [eval_function_call.cpp](../../../src/query/evaluator/expr/eval_function_call.cpp) | 每次函数调用构造 `std::vector<EvalResult>` + `std::vector<const Column*>` |
| [eval_case.cpp](../../../src/query/evaluator/expr/eval_case.cpp) | 每次 CASE 构造 `std::vector<WhenThenEval>`；逐行 `getValue/setValue` |
| [eval_map.cpp](../../../src/query/evaluator/expr/eval_map.cpp) | 每次 MAP 构造 `entry_cols`；逐行 `MapValue`/`Value` |
| [eval_property.cpp](../../../src/query/evaluator/expr/eval_property.cpp) | 行循环内 `std::vector<Value> found`，绝大多数顶点只有 0/1 个候选 |
| [eval_list.cpp](../../../src/query/evaluator/expr/eval_list.cpp) | 旧实现把 `evaluateInternal(elem, input)` 放在**行循环**里：R 行 × N 元素做 R×N 次整块求值 |
| [eval_list_comprehension.cpp](../../../src/query/evaluator/expr/eval_list_comprehension.cpp) | 每个元素构造 `DataChunk`、拷贝全部输入列；每个元素 `std::vector<bool>` |
| [eval_quantifier.cpp](../../../src/query/evaluator/expr/eval_quantifier.cpp) | 同上，每个元素构造 `DataChunk` |
| [eval_reduce.cpp](../../../src/query/evaluator/expr/eval_reduce.cpp) | 每个元素构造 `DataChunk`；每元素复制/重建 `temp_column_overrides_` |
| [aggregate_physical_op.cpp](../../../src/query/physical_plan/operator/aggregate_physical_op.cpp) | 每 chunk 构造 `vector<vector<Value>>` / `vector<vector<vector<Value>>>`，再逐行拷入 `Row` |
| `temp_columns_` | `std::deque<Column>` 只 `push_back`，从不复用；Aggregate/Sort 的 evaluator 跨 chunk 复用，池会一直增长 |
| evaluator 生命周期 | Filter/Project 每 chunk 新建；Aggregate/Sort 跨 chunk 复用但不 reset |
| 存储回调 | `scanEdgesCallback` 是 `std::function`，每边一次类型擦除调用（P2） |

旧版 c10 的采样曾显示 **80% 在堆分配/释放**；`evalMap` 的注释记录了
`collect({msg, likeTime})` 从 **28.0M 次分配降到 1.7M**。说明这条线真实存在，但当前版本
已修过一部分，收益必须重新测量。

---

## 2. 设计目标

1. **零语义变化**：NULL/三值逻辑、OPTIONAL、重复行、随机/时间函数、list/map/path 的身份语义
   全部保持；以 21/21 LDBC 结果与 TCK 为准。
2. **reentrant**：函数参数、MAP 条目、CASE 分支、list comprehension/quantifier/reduce 的
   子表达式可以嵌套；scratch 不能互相覆盖。
3. **按作用域复用**：per-query/per-operator 的 evaluator 跨 chunk 复用；per-expression 的
   临时列跨表达式复用；per-element 的临时列跨元素复用。
4. **不引入 per-row 堆分配**：常见单候选/单行路径走栈上对象或列内直写。
5. **可回退**：任何快速路径失败都回退到原 `Value` 路径，行为不变。

---

## 3. 设计

### 3.1 按求值深度分帧的 scratch

```text
EvalScratch（每个 eval_depth 一帧）
  ├─ arg_results / arg_cols          # evalFunctionCall
  ├─ case_branches                   # evalCase
  ├─ entry_cols                      # evalList / evalMap
  ├─ predicate                       # 单行谓词
  └─ single_row_chunk                # list comprehension / quantifier / reduce
```

- `eval_depth_` 在 `evaluateInternal` 入口 +1、出口 -1（RAII）。
- `scratch()` 按当前深度取帧，必要时扩容 `scratch_stack_`。
- `scratch_stack_` 必须是 `std::deque`（或其它引用稳定的容器）：外层 comprehension/function
  持有 `EvalScratch&` 期间，内层表达式可能继续增长深度；`std::vector` 扩容会让引用悬空。
- 嵌套表达式使用更深的一帧，不会覆盖外层的 `arg_results` / `entry_cols`。
- `resetForChunk()` 清空各帧的 vector（保留容量），但不销毁 `single_row_chunk` 的列，
  使其 buffer 可跨 chunk 复用。

### 3.2 临时列池

```text
temp_columns_ : std::deque<Column>   // 指针稳定，evalMap/evalCase 可持有
temp_used_    : size_t
acquireTempColumn(type, n):
  if temp_used_ < temp_columns_.size():
      col = temp_columns_[temp_used_++]
      col.resetFlat(type, n)        // 复用 ColumnBuffer
  else:
      temp_columns_.emplace_back(); col.resetFlat(type, n); ++temp_used_
```

- `ColumnBuffer::reset(type, n)`：保留已分配的 typed vector 容量；前 n 行清零并标 valid；
  与 `Column::flat` 的初始状态一致。
- `Column::resetFlat` 只在 `buffer.use_count() == 1` 时原地复用；共享 buffer（DICTIONARY）
  时分配新 buffer，避免改到别的列。
- `beginExpression()` 在**顶层** `evaluate`/`evaluatePredicate` 开始时把 `temp_used_` 归零；
  嵌套表达式不重置。
- list comprehension / quantifier / reduce 在元素循环里用 `tempMark()`/`resetToTempMark()`
  回收本元素的临时列；mark 取在“本函数自己的结果列/list 列已分配之后”，因此不会覆盖外层
  仍存活的列。

### 3.3 列级 typed 拷贝

- `evaluate` 优先 `result.copyFrom(source, count)`：
  - 同类型、两边 FLAT：`ColumnBuffer::copyFrom` 直接 `std::copy_n` + validity bitmap；
  - 失败则逐行 `copyValueFrom`（已有），再失败才 `getValue/setValue`。
- `evaluatePredicate` 对 BOOL 列直接读 `bool_data`；CONSTANT bool 直接广播；
  其他类型/形式回退 `Value`。
- `Column::getBool` 只在 BOOL/常量上返回有效值；NULL 返回 false。

### 3.4 单行 chunk 复用

list comprehension / quantifier / reduce 原来对**每个元素**构造 `DataChunk`、resize columns、
把所有输入列复制成 `Column::constant`。现在：

- 每个求值深度一个 `single_row_chunk`；
- 每个外层行只设置一次输入列常量；
- 元素循环只改 loop/acc/index 这三列（`Column::resetConstant`）；
- `evaluatePredicateOne` 直接在 1 行 chunk 上返回 `std::optional<bool>`，
  不再为每个元素分配 `std::vector<bool>`；
- list comprehension 的 where 与 projection 共享同一个单行 chunk；predicate 的临时列在
  projection 前回收。

### 3.5 list literal 的 O(R×N) 修复

`evalList` 改为：

1. 每个元素表达式在整块上求值一次，得到 `entry_cols`；
2. 行循环只读 `entry_cols[e]->getValue(row)` 组装 `ListValue`。

从 `R×N` 次整块求值降为 `N` 次，和 `evalMap` 的既有做法一致。

### 3.6 零散小缓冲

- `evalProperty`：`std::optional<Value> first_found` + `std::vector<Value> extra_found`；
  单候选不分配，多候选才分配。
- `evalFunctionCall` / `evalCase` / `evalMap`：全部改用 `EvalScratch` 的成员 vector，
  `clear()` 复用。
- `evalReduce`：`temp_column_overrides_` 每行安装一次，不再每元素复制 map。

### 3.7 evaluator 生命周期

- Filter/Project/Unwind：evaluator 提到 chunk 循环外，每 chunk `resetForChunk()`；
- Aggregate/Sort：已有跨 chunk evaluator，补 `resetForChunk()`；
- `resetForChunk` 清理 prefetch 缓存、pending ids、临时列使用计数、overrides；
  保留临时列池/列 buffer 容量。

---

## 4. 不变量

1. 任何 `Column*` / `EvalResult` 在存活期间，其对应的 `temp_columns_` 槽位不得被回收。
   - 顶层 `beginExpression` 只在没有外部引用时回收；
   - 嵌套的 `tempMark` 只在“本函数自己的结果列已分配”之后取；
   - 因此嵌套 mark ≥ 外层存活列的下标。
2. `single_row_chunk` 按深度隔离；外层 chunk 不会被内层求值覆盖。
3. `resetForChunk` 只在 `eval_depth_ == 0` 时调用；算子负责不在协程中途调用。
4. `ColumnBuffer::reset` 只对独占 buffer 调用；共享 buffer 走新分配。
5. 所有快速路径都可回退；快速路径失败不能改变结果。

---

## 5. DuckDB 参考（只借鉴结构，不复制代码）

DuckDB 的向量化执行里，以下模式和本设计同构（本次核对了
`F:\code\duckdb\src`，只借鉴结构，不复制代码）：

- `src/common/types/data_chunk.cpp` + `Vector` + `SelectionVector` 的列式批次；
- `src/execution/expression_executor/`（`execute_function.cpp`、`execute_case.cpp` 等）
  按表达式节点持有可复用的 `ExpressionState`，不在每行重新构造参数容器；
- `FlatVector`/`DictionaryVector`/`ConstantVector` 的形式区分，等价于我们的
  FLAT/CONSTANT/DICTIONARY；
- 算子拥有自己的执行状态，批间复用；表达式求值按 vector 而不是按 row。

本设计不引入 DuckDB 的完整 vectorized executor，只把“scratch 复用 + 列级 typed copy +
单行 chunk 复用”移植到现有 evaluator。

---

## 6. 测试

- 新增 `EvaluatorScratchTest`：
  - `ReusesTempColumnsAcrossEvaluations`：同 evaluator 连续 evaluate，`tempColumnCapacity` 不随次数增长；
  - `CopyFromColumnRefPreservesNulls`：typed copy 保持 NULL/值；
  - `PredicateBoolFastPathPreservesNulls`：BOOL 快路径保持 NULL→false；
  - `ListLiteralEvaluatesElementsOncePerChunk`：list literal 结果逐行正确；
  - `ResetForChunkKeepsCapacityStable`：跨 chunk reset 后容量不增长。
- 既有：`evaluator_typed_tests`、`query_executor_tests`（含 list comprehension / reduce /
  pattern comprehension 用例）、全量 ctest + TCK。
- 本轮已跑：`evaluator_typed_tests` 30/30；`query_executor_tests` 的
  list comprehension / reduce / quantifier 过滤组 7/7；全量 `query_executor_tests`
  **609/609**。全量 ctest/TCK 待提交前补跑。
- 性能：同机交错 A/B；重点 c7/c10/c14/c3；用 allocation counter / GDB 采样确认
  `operator new` / `malloc` 热点下降；不看存储受限的 c5/c9 判断 evaluator 收益。

---

## 7. 风险

| 风险 | 缓解 |
|---|---|
| 临时列被提前复用，指针悬空 | 只在顶层/显式 mark 回收；测试覆盖嵌套 list/map/case/reduce |
| 单行 chunk 的列状态残留 | 每行重设输入列；每元素重设 loop/acc/index；NULL 语义测试 |
| `ColumnBuffer::reset` 改到共享 buffer | `use_count()==1` 检查；共享时新分配 |
| list literal 一次求值改变随机函数语义 | 函数在整块上仍按行产生值；与 `evalMap` 既有语义一致；TCK |
| 内存保留（池只增不减） | 池上限由“单表达式最大临时列数”决定；不做无限增长 |
| 性能收益被存储/计划淹没 | 先 A/B + allocation counter；不承诺端到端倍数 |

---

## 8. 后续

1. ~~Aggregate typed 路径~~：已落地（chunk 级 typed 参数列 + `AggregateInput` 行视图 +
   captureless 回调），见 [aggregate-typed-execution-design.md](aggregate-typed-execution-design.md)。
2. ~~存储 scan callback 去 `std::function`~~：已落地（`folly::FunctionRef`），见
   [scan-callback-design.md](../../storage/scan-callback-design.md)。
3. ~~typed 比较 kernel 补全~~：已落地（INT64/DOUBLE EQ/NEQ、BOOL 三值 AND/OR/XOR、
   STRING EQ/NEQ 快路径、`__edge_unique` 原生 kernel），见
   [value-copy-audit.md](value-copy-audit.md)。
4. ~~join/apply 的 typed 相关列与结果平移~~：已落地（`CorrelatedSource::setRowFromChunk`
   + 结果列 `copyValueFrom`）。
5. **typed GROUP BY key / Sort/Materialize 的 Row 物化**：Aggregate 每行构造 `Row` key；
   Sort 全量路径与 Materialize 缓存整行 `Value`；属于下一步的批量/列式化。
6. **编译器自动 SIMD**：在 typed path 和分配问题解决后，用连续内存/简单循环让 `-O3`
   自动向量化；不写 intrinsics。
7. **intra-query 并行**：morsel-driven；高风险，另行设计。

完整审计、代码位置与 LDBC 证据见 [value-copy-audit.md](value-copy-audit.md) 与
[comparison §3.10](../../benchmark/ldbc-snb-sf0.1-comparison.md)。
