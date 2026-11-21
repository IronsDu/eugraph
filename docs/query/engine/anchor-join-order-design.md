# 选择性 Anchor / Join Order 设计

> 目标：用通用规则恢复 c6/c9 这类多跳 pattern 的性能，不按 label/edge/query 名分支。
> 本文件是 Issue #326 的后续实现设计；当前代码尚未实现。

## 1. 输入

- 逻辑计划中的 Filter/Join/Expand/VarLenExpand/Project/Aggregate；
- Catalog/Stats：
  - label 顶点数；
  - edge label 选边数/平均度（需补全）；
  - 过滤后选择率、索引可用性；
- 列的 slot/身份模型必须保持不变（§4.4）。

## 2. Anchor 选择

1. 把 Filter(Cross/InnerJoin) 中等值条件下推为 join condition（已有 join_conditions）；
2. 对每个 join/expand 分支计算上界基数：
   - 带索引/强过滤的 scan 优先；
   - 小 cardinality 分支作为 anchor；
3. 从 anchor 沿边扩展，优先选择：
   - 目标侧有索引；
   - 过滤后基数小；
   - 度数低；
4. 无法估算/无索引时保留原计划，不做猜测。

## 3. SemiJoin / 去相关

- `collect(distinct x)` + `UNWIND` + membership/IN → semi-join；
- correlated equalities → hash semi/index probe；
- 只做能证明等价的重写；null/重复行/LEFT 语义必须显式测试。

## 4. 物理边界

- Filter 需要能在 IndexScan/LabelScan 之上、PE 之下执行；
- ProjectionExtract 必须保留 slot/列契约；
- 不允许为了性能把 Filter 移到会改变 null/重复行语义的位置。

## 5. 与现有规则的关系

- JoinCommute/Associate 只提供搜索空间；
- Anchor/JoinOrder 由 cardinality/selectivity 选择候选，不按数据名字；
- HashJoin/IndexJoin/NestedLoop 仍由 CBO 比较。

## 6. 分阶段

1. 补 edge 平均度/索引选择性统计，并让 LogPropDeriver 消费；
2. 实现 anchor 选择的 logical rule，先覆盖 2–3 表 join；
3. 接入 SemiJoin；
4. 物理 Filter/PE 边界；
5. 用 c6/c9 + 非 LDBC schema 做 A/B。

## 7. 验收

- c6/c9 结果与当前逐行一致；
- 同二进制交错 A/B，记录 min/p50；
- 无 label/query 名硬编码；
- CBO winner 不为 null；
- ASan/UBSan 通过。
