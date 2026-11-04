# 查询执行热路径的 Value 拷贝审计

> 状态：随 `perf/evaluator-scratch-reuse` 分支落地第一批。
> 目标不是“消灭所有 Value”，而是把**不需要所有权**的读取从 Value 物化改成
> typed 访问/借用；确实需要跨 chunk 持有的数据仍然必须拥有。

## 1. 判据

在行循环里看到下面这些形状，先假设它是可疑点：

```cpp
Value v = column.getValue(i);                  // 每行一次 variant 构造
const auto& v = column.getValue(i);            // const& 绑定临时量，仍然是拷贝
std::vector<Value> vals = ...;                 // 每行/每参数一个容器
std::vector<std::vector<std::vector<Value>>>;  // 参数张量
std::function<...> callback;                   // 热路径类型擦除
```

需要区分三类：

1. **比较/判定**：只需要读，不应物化 Value；
2. **搬运**：从一列写到另一列，优先 `Column::copyValueFrom`/typed copy；
3. **拥有**：list/map/collect/group key/sort key 必须持有值，不能返回 view。

## 2. 新增的统一入口

`src/query/dataset/cell_access.hpp`：

```cpp
std::string_view columnStringView(const Column&, size_t row);
VertexId columnVertexId(const Column&, size_t row);
bool columnCellEquals(const Column&, size_t, const Column&, size_t);
bool columnCellEqualsValue(const Column&, size_t, const Value&);
bool copyColumnCell(const Column& src, size_t src_row, Column& dst, size_t dst_row);
```

- 标量/字符串走 typed buffer；
- 其他形式回退 `getValue`，保证语义；
- NULL 语义由调用方决定（helper 本身把 NULL 视为不相等）；
- `columnCellEqualsValue` 保留 `valueEquals` 的 int64/double 数值提升。

这样后续代码不用在每处重新写 `if (type==INT64) ...` 的零散分支。

## 3. 本轮已处理

| 位置 | 旧形状 | 现在 |
|------|--------|------|
| `ExpressionEvaluator::evalCase` 简单 CASE | 每 branch/row 复制 subject + when + then/else | `columnCellEquals` + `copyColumnCell`；BOOL WHEN 直接 `getBool` |
| `evalBinaryOp` XOR | 每行两个 `Value` | BOOL 三值逻辑 typed kernel，无 Value |
| `evalBinaryOp` IN | 每行复制左侧 Value | 标量/字符串左侧用 `columnCellEqualsValue`；仅通用类型回退 Value |
| Aggregate 参数 | `vector<vector<vector<Value>>>` + 每行 `vector<Value>` | chunk 级 typed 参数列 + `AggregateInput` 行视图（见 aggregate-typed-execution-design.md） |
| `ExpressionEvaluator::evaluate` | 逐行 `getValue/setValue` | 整列 `copyFrom` / 单行 `copyValueFrom` 快路径 |
| `mutation_mirror` 物化 | `fresh.setValue(i, col.getValue(i))` | `copyColumnCell` |
| `REMOVE`/`SET` 的 COW 物化 | 同上 | `copyColumnCell` |
| `Expand` 精确邻接 seek / 普通 dst 过滤 | 每行 `getValue` 再取 vertex id | `columnVertexId` 直接读 VERTEX_REF/VERTEX/INT64 列 |
| `SemiJoin` 批量右键 | 每个右行一个 `vector<VertexId>` | 扁平 `right_flat` + `span` 视图；右侧扫描用 `columnVertexId` |
| `evalBinaryOp` EQ/NEQ/AND/OR | 逐行 `getValue`（`genericEqBatch`/`boolAndBatch`） | INT64/DOUBLE typed `EqOp`/`NeqOp`；BOOL 三值 AND/OR/XOR typed；STRING EQ 的 FLAT/CONSTANT 快路径 |
| `__edge_unique` | `scalarToBatch` 每行 `vector<Value>` + 新 `unordered_set` | captureless `edgeUniqueBatch`，typed EDGE/EDGE_KEY 列 + 复用 set |
| `CorrelatedSource` 注入 | 每左行 `vector<Value>` + `rows_` 复制 | `setRowFromChunk` view + 复用 `view_values_`；四种相关算子接入 |
| CrossProduct/Apply/HashJoin/CVP/LeftJoin 输出 | 逐列 `getValue`+`setValue` | `copyValueFrom` 优先，失败回退 |
| `Distinct` digest | 逐列 `getValue` 后哈希 | `cellDigestColumn` 直接读 typed 列/顶点 id |
| PE 属性写入 | `propertyValueToValue` 构造 Value 再 `setValue` | `setColumnFromPropertyValue` 标量/字符串直写 |

## 4. 仍然必须拥有的拷贝（不是 bug）

- `ListValue`/`MapValue`/`collect()` 的结果必须拥有元素；
- `Row` 作为 GROUP BY / DISTINCT / SORT 的键必须跨 chunk 持有；
- `BoundPropertyRef`/动态属性读取的公共 API 返回 `Value`（属性可能来自任意标签）；
- 标量函数 `ScalarFn` 的 `const vector<Value>&` 边界：函数可能返回新容器；
- `Column::getValue` 作为通用 fallback 仍然存在，只是不应在已知 typed 的热循环里首选。

这些点如果后续要改，需要先改数据模型（例如 list 元素借用/arena、typed group key），
不能靠局部 `const&` 掩盖。

## 5. 后续候选（2026-11-04 第二轮后）

1. **Sort/Materialize/HashJoin 的整行 `Row` 物化**：HashJoin 输出列已 typed copy，但
   Sort 全量路径、Materialize 缓存、HashJoin 的 `Key=vector<Value>`/右行仍是 `vector<Value>`；
   top-N 比较器也逐次 `getValue`。需要独立的列式缓冲/排序键设计。
2. **typed GROUP BY key**：Aggregate 仍每行构造 `Row` key；本轮只做了 DISTINCT 的 typed
   digest，未改 group map。
3. **per-group batch aggregate update**：`AggregateInput` 仍是单行视图；selection vector/
   批量 update 未做。
4. **属性字符串借用**：本轮用 `setColumnFromPropertyValue` 去掉 Value 中转，但
   `setString` 仍复制一次；真正的 string_view borrow 需要属性存储生命周期契约。
5. **`FunctionFn` / `fallback_fn` 的 `std::function`**：每次表达式求值一次，非逐行，
   优先级最低。
6. **短查询固定开销**：Bolt/执行器启动/线程切换约 0.3–0.5 ms/查，不属于本优化族。

## 6. 验证结果（2026-11-04）

- 单元/回归：`data_chunk_tests` 20/20（`CellAccess` 3/3）、`query_executor_tests`
  **614/614**、`evaluator_typed_tests` **34/34**；TCK **3930/3983**（唯一失败为既有
  `Temporal2 [6]`），新增 aggregate-typed-regressions 6/6；storage callback 相关
  `wt_tests` 7/7、`index_store_tests` 13/13、`metadata_tests` 20/20、
  `index_e2e_tests` 56/56。
- LDBC 对照（2026-11-04，Release）：21/21 结果一致。第二轮稳态 complex 696.1 ms vs
  neo4j 304.1 ms（2.29×）；第一轮（增量前）为 637.0 vs 269.8（2.36×），两者受 CPU
  频率影响，只在同轮内比较。机制与完整表见
  [comparison §2/§5.9](../../benchmark/ldbc-snb-sf0.1-comparison.md)。
- 同二进制 A/B（第二轮，40 轮）：c5 0.957、c10 0.961、c7 0.966、c14 0.993、c1 1.027、
  c12 1.011（opt/base min）；其余在 ±8% 噪声内。稳定收益 3–4%，不是数量级。

## 7. 验证

- `test_data_chunk` 新增 `CellAccess`：string/scalar 比较、NULL、typed copy；
- `query_executor_tests` 的 CASE / IN / XOR / SET / REMOVE 用例；
- 全量 `query_executor_tests` + storage/index 测试；
- 性能结论仍需同二进制交错 A/B，不能用“少了几个 Value”代替测量。
