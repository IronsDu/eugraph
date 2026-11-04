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
| `evalBinaryOp` XOR | 每行两个 `Value` | 两侧 BOOL 时 `getBool`，无 Value |
| `evalBinaryOp` IN | 每行复制左侧 Value | 标量/字符串左侧用 `columnCellEqualsValue`；仅通用类型回退 Value |
| Aggregate 参数 | `vector<vector<vector<Value>>>` + 每行 `vector<Value>` | chunk 级 typed 参数列 + `AggregateInput` 行视图（见 aggregate-typed-execution-design.md） |
| `ExpressionEvaluator::evaluate` | 逐行 `getValue/setValue` | 整列 `copyFrom` / 单行 `copyValueFrom` 快路径 |
| `mutation_mirror` 物化 | `fresh.setValue(i, col.getValue(i))` | `copyColumnCell` |
| `REMOVE`/`SET` 的 COW 物化 | 同上 | `copyColumnCell` |
| `Expand` 精确邻接 seek / 普通 dst 过滤 | 每行 `getValue` 再取 vertex id | `columnVertexId` 直接读 VERTEX_REF/VERTEX/INT64 列 |
| `SemiJoin` 批量右键 | 每个右行一个 `vector<VertexId>` | 扁平 `right_flat` + `span` 视图；右侧扫描用 `columnVertexId` |

## 4. 仍然必须拥有的拷贝（不是 bug）

- `ListValue`/`MapValue`/`collect()` 的结果必须拥有元素；
- `Row` 作为 GROUP BY / DISTINCT / SORT 的键必须跨 chunk 持有；
- `BoundPropertyRef`/动态属性读取的公共 API 返回 `Value`（属性可能来自任意标签）；
- 标量函数 `ScalarFn` 的 `const vector<Value>&` 边界：函数可能返回新容器；
- `Column::getValue` 作为通用 fallback 仍然存在，只是不应在已知 typed 的热循环里首选。

这些点如果后续要改，需要先改数据模型（例如 list 元素借用/arena、typed group key），
不能靠局部 `const&` 掩盖。

## 5. 后续候选（按收益/风险排序）

1. **typed GROUP BY key**：当前 `Row` 每行仍构造 key Value；可为 1–2 个定长 key 做
   typed hash/equal，避免 Value 中转。
2. **字符串属性读取**：`BoundPropertyRef` 的结果若是 STRING，可增加 string_view
   borrow API，避免属性字符串复制；需要保证属性存储生命周期覆盖表达式求值。
3. **Aggregate batch update**：把 `AggregateInput` 的 row 视图升级为 per-group
   selection vector（DuckDB `aggregate_update_t` 形态），一次更新一组行。
4. **投影/提取算子的 dynamic path**：`ProjectionExtractPhysicalOp` 仍有
   `getValue` 分支；定长/同类型列可走 `copyColumnCell`。
5. **标量函数边界**：逐步把高频标量函数从 `ScalarFn` 迁到 columnar fn（本审计不展开）。

## 6. 验证结果（2026-11-03）

- `data_chunk_tests` 20/20，其中新增 `CellAccess` 3/3（string/scalar/NULL/ANY/typed copy）；
- `query_executor_tests` **614/614**（含 CASE/IN/XOR/SET/REMOVE 与全部 aggregate 回归）；
- TCK **3930/3983**；唯一失败是 temporal `Temporal2 [6]`（与本轮改动无关的既有日期时区用例），
  另有 52 个 TCK undefined step；受影响 aggregate/mutation/precedence 子集 **178/178**；
  新增 aggregate-typed-regressions 6/6；
- storage callback 相关：`wt_tests` 7/7、`index_store_tests` 13/13、
  `metadata_tests` 20/20、`index_e2e_tests` 56/56；
- 性能 A/B 尚未执行；本审计只证明语义与分配形态，不把“少了 Value”当成倍数。

## 7. 验证

- `test_data_chunk` 新增 `CellAccess`：string/scalar 比较、NULL、typed copy；
- `query_executor_tests` 的 CASE / IN / XOR / SET / REMOVE 用例；
- 全量 `query_executor_tests` + storage/index 测试；
- 性能结论仍需同二进制交错 A/B，不能用“少了几个 Value”代替测量。
