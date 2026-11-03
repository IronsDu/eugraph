# Aggregate Typed Execution 设计

> 状态：已确认，随 `perf/evaluator-scratch-reuse` 分支落地第一批。
> 目标：把 AggregatePhysicalOp 从「每行构造 `std::vector<Value>`，再走
> `std::function<void(AggStateBase&, const std::vector<Value>&)>`」改成
> 「每 chunk 求值一次 typed 列，按行传递列视图，函数指针回调」。

## 1. 问题与证据

旧实现的形状（`src/query/physical_plan/operator/aggregate_physical_op.cpp`）：

```cpp
std::vector<std::vector<std::vector<Value>>> chunk_arg_vals(aggregates_.size());
// [aggregate][row][argument]
...
std::vector<Value> vals = std::move(chunk_arg_vals[i][r]);
agg.func_def->agg_update(*state.agg_states[i], vals);
```

同时 group key 也先落成 `std::vector<std::vector<Value>> chunk_gk_vals`，再逐行
拼 `Row`。这带来四类固定开销：

1. 每个 aggregate × 每个 row × 每个 argument 一个 `std::vector<Value>` 头；
2. 每个 argument 一次 `Column::getValue`（variant 构造），即使调用方只需要
   `int64_t`/`double`；
3. `agg_update` 是 `std::function`，每行一次类型擦除间接调用；
4. group key 在进入 hash map 前多一次 `Value` 中转。

LDBC/profile 中 `AggregatePhysicalOp` 在 complex-10/14 一类查询里占比可达两位数，
且大头是分配与 Value 往返，不是聚合算术本身。

## 2. 设计目标与非目标

**目标**

- 聚合参数在 chunk 级只求值一次，按列持有；行循环不再构造参数容器。
- 聚合回调不再使用 `std::function`；聚合函数是 captureless 函数指针。
- 常见聚合（count/sum/avg）走 typed payload；通用聚合（min/max/collect/
  percentile）保留 `Value` 语义，但不再经过参数 `vector<Value>`。
- null / `count(*)` / DISTINCT / 复杂聚合替换的语义保持原样。

**非目标（本轮不做）**

- 不改 KV 布局、不加二级索引。
- 不引入 intra-query 并行或手写 SIMD。
- 不做跨 group 的批量 update/selection vector（DuckDB `aggregate_update_t` 的
  batch 形态）；本轮先把“每行参数容器 + 每参数 Value”消掉。批量化留到 A/B
  证明 update 循环仍是主瓶颈后再做，避免一次改两个变量。
- 不重写 `FunctionDef` 的标量 `FunctionFn`（仍是 `std::function`，与聚合路径无关）。

## 3. 核心接口

`src/query/function/function_def.hpp`：

```cpp
struct AggregateInput {
    // 非拥有视图：指向本 chunk 已求值好的参数列。
    std::span<const Column* const> args;
    size_t row = 0;
};

using AggInitFn = std::unique_ptr<AggStateBase> (*)();
using AggUpdateFn = void (*)(AggStateBase&, const AggregateInput&);
using AggFinalizeFn = Value (*)(const AggStateBase&);
```

- `Column` 在 `function_def.hpp` 中保持前置声明；`AggregateInput` 只存指针，
  不让低层函数定义头依赖 `data_chunk.hpp`。
- 函数指针代替 `std::function`：注册处的 captureless lambda 可隐式转换；
  这也和 DuckDB `AggregateFunctionCallbacks` 用函数指针的形态一致。
- `AggregateInput` 是 **call-scoped view**：列和 span 由调用方在 update 调用期间
  持有；聚合状态不得保存该视图。

## 4. 算子执行结构

`AggregatePhysicalOp::executeChunk` 的新流水线：

```
每个 child chunk:
  1. evaluator.resetForChunk()
  2. 求值 group key 表达式 -> std::vector<Column> key_columns
  3. 对每个真实 aggregate:
       求值每个 argument 表达式 -> std::vector<Column> arg_columns[a]
       复杂聚合不建参数列；count(*) 仍有一列 literal-null 参数，
       update 回调忽略它，null 跳过标志在 chunk 级预计算
  4. 逐行:
       从 key_columns 取 Value 组 Row key
       find/create GroupState
       对每个 aggregate:
         null / count(*) / DISTINCT 策略（与原实现同一判定）
         AggregateInput{arg_ptrs[a], row}
         agg_update(*state, input)
  5. 逐 group finalize，输出 FLAT DataChunk
```

关键点：

- `chunk_arg_vals` 和 `chunk_gk_vals` 删除；参数列只在 chunk 级存在。
- 每行不再构造 `std::vector<Value> vals`；`AggregateInput` 只是两个字段的 view。
- `count(*)` 在 chunk 级预计算标志，不在行循环里反复做
  `holds_alternative<BoundLiteral>`。
- `DISTINCT` 仍需一个 `Value` 作为去重 key（hash set 的键），但它只针对
  `args[0]`，不再为所有参数构造 Value。
- 复杂聚合（`func_def == nullptr`，内部 aggregate 替换）保持原路径；它只发生在
  输出阶段、每 group 一次，不在热行循环里。

## 5. Typed 访问

`Column` 增加：

```cpp
int64_t getInt64(size_t i) const;
double  getDouble(size_t i) const;
```

- INT64 列直接读 `int64_data`；DOUBLE 列直接读 `double_data`；
- INT64/DOUBLE 交叉读取做与 `setValue` 一致的数值转换；
- ANY 列回退到 `getValue` 后取 variant；非数值类型返回 0，和旧
  `Int64SumState::add`/`DoubleSumState::add` 对非匹配类型“忽略”的语义一致；
- null 由算子统一跳过，typed accessor 不重复做 null 分支。

聚合状态：

- `CountState`：只 `++value`；
- `Int64SumState::add(int64_t)`、`DoubleSumState::add(double)`、`AvgState::add(double)`；
- `MinMaxState`/`CollectState`/`PercentileState`：继续接收 `Value`，因为语义本身
  需要通用比较/存储；
- `PercentileState::capturePercentile` 从 `args[1].getValue(row)` 取，值/百分位
  参数都走列视图。

## 6. Null / DISTINCT / count(*) 不变量

与原实现逐条对齐：

- `count(*)`：`has_variadic_args && arguments[0]` 是 null literal；不建参数列，
  不跳过 null，每行 `++count`。
- 其他聚合：`args[0]` 为 null 时跳过；例外是 `keeps_nulls && !distinct`
  （collect 非 DISTINCT），它保留 null。
- `DISTINCT` 永远跳过 null（即使 `keeps_nulls`）；去重键是 `args[0]`。
- 空输入 + 无 group key：仍输出一行 `agg_init` + `agg_finalize` 的默认值。
- 复杂聚合：内部 aggregate 先 finalize，再作为 substitution 求值外层表达式。

## 7. 与 DuckDB 的对应关系

参考 `F:\code\duckdb\src`（只借鉴结构）：

- `src/include/duckdb/function/aggregate_function.hpp`：聚合回调是函数指针
  （`aggregate_update_t` 等），不是 `std::function`；
- `src/execution/aggregate_hashtable.cpp`：输入先求值成 `Vector`，再按 group
  状态更新；
- DuckDB 的 `aggregate_update_t` 是 batch（`Vector inputs[], count`）。本设计
  先采用 row view，保留后续升级为 batch 的空间；不为了“像 DuckDB”而一次引入
  selection vector 和 group-row 缓冲。

## 8. 测试与验证

- `tests/test_query_executor.cpp` 的 `QueryExecutorAggTest`：count/sum/avg/
  min/max/distinct/group by/empty input/order by/skip limit；
- 新增 typed 专项：
  - `UNWIND range(1, 3000)` 的 sum/avg/count：跨多个 chunk；
  - int64 与 double 混合的 sum/avg；
  - null 的 `sum`/`count(x)`/`count(*)`/`collect`；
  - 多 group key + 多 chunk；
  - 复杂聚合 `size(collect(...))` 回归。
- 全量 `query_executor_tests`（当前基线 609）必须保持全绿。
- 性能判据：同二进制、交错顺序 A/B；不在没有数据/服务环境时宣称倍数。

## 9. 落地与验证（2026-11-03）

- `query_executor_tests` **614/614**，包含新增的跨 chunk / double / null+collect /
  多 group / 复杂聚合用例；`count(DISTINCT m)` 与 `WITH DISTINCT m` 的一致性回归通过。
- `data_chunk_tests` 20/20（`CellAccess` 3/3）。
- 关键坑：聚合参数不能一律按 `getBoundExprType` 建 typed 列。图实体在拓扑阶段是
  `VERTEX_REF`、语义阶段是 `VERTEX`；强制 typed 列会让表示不匹配的写入只置 validity
  位而 payload 为空，`count(DISTINCT m)` 因此退化成 1。现在只对 BOOL/INT64/DOUBLE/
  STRING 建 typed 列，其他类型一律 ANY。
- 尚未做：per-group batch update（selection vector）、typed group key hashing、性能 A/B。

## 10. 风险

- `getInt64/getDouble` 的转换必须与旧 `Value` 路径一致；专项测试覆盖 int64/double/
  null，其他类型由通用 `Value` 聚合覆盖。
- `AggregateInput` 是 view；任何聚合状态不得保存 `args` 指针或 `span`。
- 函数指针不能捕获；注册处的 lambda 必须保持 captureless；否则编译期报错。
- 复杂聚合与空输入路径最容易漏，测试必须覆盖 `size(collect(...))` 和空表。
