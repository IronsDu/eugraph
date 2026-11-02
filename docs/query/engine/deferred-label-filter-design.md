# 延迟目标标签判定（Expand dst label filter → PE 物化时过滤）

> 状态：**设计 + 原型（原型已回退，待 label 顶点数统计门控）**。目标：消除 LDBC c9（`label_fwd_Message` 286,744 行扫描，
> 实测 ~26 ms）与 c6（`label_fwd_Post` 135,701 行，13.3 ms）这类“目标标签成员判定”
> 成本；不改变查询语义，不新增 KV 编码/Thrift 字段。

## 1. 现状与根因

`BoundExpandOp.dst_label_ids` 在 `ExpandPhysicalOp` 里做目标标签过滤：

- 邻居数 < 4096：`getVertexLabelsBatch(neighbors)`，每个邻居一次 `search_near`；
- 邻居数 ≥ 4096：`scanVerticesByLabel(label)` 扫**整张** `label_fwd_{label}` 建成员集合，
  再用集合过滤邻居。

第二种策略在 c9/c6 上是纯浪费：c9 为 53,192 条边扫 286,744 行（~26 ms），c6 为 53,192
条边扫 135,701 行（13.3 ms）。而下游 `ProjectionExtract`（PE）本来就要读同一批目标顶点
的属性（c9 的 53k 次 coalesce，~65 ms），标签行（`vertex_data` kind 0x00）与属性行
（kind 0x01）在**同一个 `{vid}` 前缀**下——可以顺带读出来，零额外 B-tree 下降。

## 2. 不变量（决定“能延迟到哪里”）

Expand 的目标标签过滤只决定**该行是否继续**。因此把它移到下游是安全的，当且仅当：

1. 从 Expand 到执行该过滤的算子之间，**没有改变行数/行身份语义的算子**
   （Aggregate / Sort / Limit / Distinct / LeftJoin / PC Apply / VarLenExpand 等）；
2. 目标顶点在其间没有被当作“完整顶点”使用（`labels()`、`n:Label` 谓词、ConstructVertex
   之外的属性读取可以，因为属性读取不依赖标签集合）；
3. 过滤在使用该顶点的所有分支之前执行（plan 是树，PE 是 Expand 的唯一父节点即可）。

最简且可证明安全的落点：**Expand 的直接父节点恰好是 materialize 该 dst 变量的
`ProjectionExtractPhysicalOp`**，且该 PE 的 spec 已包含 `LoadVertexPropCoalesce` 或
`ConstructVertex`（c9 是前者）。其他形态（c6 的 Expand 后面直接接另一个 Expand）不适用，
需要单独的 labels-only PE（见 §6）。

## 3. 设计

### 3.1 存储 API

新增（`IAsyncGraphDataStore` / `AsyncGraphDataStore` / `SyncGraphDataStore`）：

```cpp
struct VertexDataBatchEntry {
    std::vector<PropertyValue> values;  // 按 coalesce_candidates 顺序
    LabelIdSet labels;                  // 该顶点的全部标签（不含 INVALID）
};
Task<std::vector<VertexDataBatchEntry>>
batchGetVertexCoalescePropertiesAndLabels(const std::vector<VertexId>& vids,
                                          const std::vector<std::pair<LabelId, uint16_t>>& candidates);
```

实现：沿用 `getVertexCoalescePropertiesBatch` 的“按 vid 排序探测 + 复用 cursor/prefix”
骨架，把前缀从 `{vid}{0x01}` 改为 `{vid}`，循环中按 kind 分派：

- `kind == 0x00` → `decodeVertexDataLabelKey`，插入 labels；
- `kind == 0x01` → 命中 candidate 则 decode value；
- `kind == 0x02/0x03` → 停止（邻接记录与本次无关）。

一次 B-tree 下降 + (标签行数 + 属性行数) 次 `next`，替代“整表扫描 + 另一次属性下降”。
标签行通常 2–4 行（Message 为 Message+Post/Comment），成本可忽略。

### 3.2 PE（ProjectionExtract）

- `ColumnSpec` 增加 `std::vector<LabelId> required_labels`（空 = 不过滤）。
- Prefetch：当 spec 是 `LoadVertexPropCoalesce` 且 `required_labels` 非空，调用上面的
  新 API，把 values 存进现有 `vertex_coalesce_cache`，labels 存进新增的
  `vertex_label_filter_cache[i][row]`。
- 执行：先按 spec 计算每行 `keep`（labels 含全部 required_labels）；然后**压缩输出**：
  - 维护 `dst` 行号，所有 spec 写入 `dst` 而不是 `row`；
  - `copyValueFrom(src, row, dst)` / `setValue(dst, …)` / typed 直写用 `dst`；
  - `output.count = dst`；CONSTANT 列保持广播，不需要逐行写；
  - validity 只对 `dst` 行有意义，尾部残留不影响（下游只看 count）。
- 无 required_labels 时 `dst == row`，零行为变化（编译期无法区分，运行时布尔判断即可；
  建议把 `keep` 为空时走原循环，避免额外分支拖慢所有 PE）。

### 3.3 Planner 接入（保持“有 PE 才延迟”）

在 `planBoundOperator` 的 `BoundExpandOp` 分支：

1. 照常构造 `ExpandPhysicalOp`（带 `dst_label_ids`）并 `dispatchProjectionExtract`；
2. 若返回的 `PlanOperatorResult.op` 是 `ProjectionExtractPhysicalOp`，且该 PE 有
   `LoadVertexPropCoalesce`/`ConstructVertex` spec 的 `source_col` 对应 dst 变量列，
   且 Expand 的 dst 变量没有被其它算子消费（树结构保证）：
   - 调 `expand->clearDstLabelFilter()`（新增 setter，清 `dst_label_ids_`）；
   - 调 `pe->setRequiredLabels(source_col, v.dst_label_ids)`；
3. 否则保持现状（Expand 内部过滤），无行为变化。

这样“是否延迟”由物理计划形状决定，不依赖标签表大小的估计；c9 命中，c6 不命中。

### 3.4 只读门控

与 PC 邻居缓存一致：仅在 `ctx.read_only` 时允许延迟。写语句不延迟（避免 Expand 与 PE
之间出现写入改变标签）。

## 4. 预期收益

| 查询 | 现状 | 延迟后 | 依据 |
|---|---:|---:|---|
| c9 | ~117 ms | **−20~26 ms** | 去掉 286k 行 label 扫描；coalesce 顺带读 2–4 行标签 |
| c14 | ~85 ms | −2~4 ms | 目标 Person 的 label 点查可省（PE 已在读 b 属性） |
| c3 | ~98 ms | 0 | 无 dst label 过滤 |
| c6 | ~64 ms | 0（首版） | Expand 后直接是另一个 Expand，无 PE；见 §6 |

判据：`PROFILE` 中该 Expand 的 `scanRows` 从 339,936 降到 53,192，`wtSN` 不增加；
同机交错 A/B c9 ≥10% 下降；21/21 结果一致。

## 5. 测试

- 新增单测（`test_query_executor.cpp`）：
  1. 目标标签过滤结果正确（含多标签、非目标标签被丢弃）；
  2. 延迟路径确实生效（可加 `ProjectionExtractPhysicalOp::labelFilterDeferredCount()` 诊断计数）；
  3. 写语句不延迟（构造一个 CREATE+PC/Expand 的语句，计数为 0）。
- 回归：`query_executor_tests`、全量 `ctest`（TCK 含大量 label/pattern 语义）、
  `verify_ldbc_results.py` 21/21。
- 存储单测：新 API 的 values/labels 与现有 coalesce + labels 组合结果一致（同一顶点、
  多标签、缺属性、空顶点）。

## 6. 后续扩展（不在首版）

- c6 形态：在 Expand 后插入 `LoadVertexLabels` + 行压缩 Filter（不依赖 PE 已物化属性），
  或把延迟判定改成“只要下游存在 labels-only PE 即可”。
- `ConstructVertex` 形态：PE 已经读 labels，可直接复用，不增加 IO。
- 若延迟条件不满足但边数远小于标签表，保留现有“全表扫描 vs 点查”启发式即可。

## 7. 与其它方向的关系

- 该改动不触碰 KV 编码/Thrift，兼容面小；
- 与 PC 邻居缓存、prefetch 快路径互不依赖；
- 若要进一步消除 c5 的 89k 次 per-edge seek，仍需 planner 半连接/反向展开（见
  [ldbc-plan-quality-analysis.md](../benchmark/ldbc-plan-quality-analysis.md) §5）。

## 8. 原型实测与回退（2026-11-02）

按本设计实现了首版：存储层合并扫描 + PE 行压缩 + planner“直接父节点是 coalesce PE 且只读”
门控。同机交错 A/B（15 轮，min；两棵二进制、两份数据副本）：

```text
query        base min   opt min   opt/base     (对调数据副本)
complex-9     112.69     105.67     0.938      124.90 → 110.32（0.883）
complex-14     85.47      81.59     0.955       89.51 →  85.15（0.951）
complex-3      91.37      87.93     0.962
complex-5     181.08     186.97     1.033      194.85 → 201.76（1.035）
complex-6      63.35      64.80     1.023
```

- 结构判据成立：c9 的 Expand `scanRows` 从 **339,936 降到 53,192**（286,744 行标签扫描消失），
  结果 20/20 一致；PE 的兼容行压缩未改变任何查询结果。
- **但 c5 稳定 +3.3~3.5%**：门控只判断“PE 直接父节点 + coalesce spec”，没有判断目标标签表
  大小。c5 的 `Person`(1,528) / `Forum`(13,750) 标签用现有 forward scan 只要 0.2–1.4 ms，
  延迟后改成逐顶点读标签行（29k–89k 次）反而更贵。c9 的 `Message`(286k) 才是收益来源。
- 结论：**需要 label 顶点数统计（或运行期代价判据）作为门控**，否则会用一个查询的收益换
  另一个查询的回归。首版已整体回退；本设计保留，等统计/门控方案确定后再实现。

### 8.1 门控需要的输入

- 目标标签的顶点数 `label_count`（或 `label_fwd` 行数）；
- 当前 batch 的边数 `edges`（运行期已知）；
- 现有策略成本模型：`edges >= 4096` 时 forward scan ≈ `label_count * 90ns`；
  延迟读标签 ≈ `edges * 标签行数 * ~75ns`。只有当 `edges * 2 < label_count * 0.6`
  时延迟才划算。
- `label_count` 的廉价来源：meta 中维护每标签计数（loader/写入时增量维护，或在 open 时
  统计 `label_fwd` 一次并接受写后轻微漂移——仅作启发式，不影响正确性）。
