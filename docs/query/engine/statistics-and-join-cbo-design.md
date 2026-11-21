# 统计信息与 Join CBO 统一设计

> 状态：设计定稿，分阶段实现。目标不是“让某个 LDBC 查询变快”，而是让 CBO 在
> 任意 schema 上用统计和代价选计划；禁止按查询文本/query id/label 名/关系名分支。

## 1. 目标与非目标

**目标**

- 统计信息与用户数据**同事务**持久化（stats journal），避免“数据提交了、统计丢了”；
- 事务本地 `StatsDelta`，提交后合并；并发合并不丢更新；
- 统计快照不可变，规划器无锁读取；缺失/过期时 fail-safe 回退；
- Cascades CBO 能产生并比较 `HashJoin`、`IndexJoin`、`NestedLoop`、`CrossProduct` 候选；
- 逻辑计划不出现 `BoundHashJoinOp`；join 算法属于物理选择；
- 最终用同二进制 A/B 验证性能，结果行必须一致。

**非目标**

- 不保证统计精确实时；允许近似/采样/过期，但必须显式标记；
- 不在本阶段做分布式/多实例统计租约；
- 不做按 benchmark 名字特化的规则。

## 2. 统计模型

### 2.1 快照

```cpp
struct GraphStatsSnapshot {
    uint64_t epoch = 0;             // 统计发布代数
    uint64_t applied_journal_seq = 0;
    bool approximate = true;

    std::unordered_map<LabelId, LabelStats> labels;
    std::unordered_map<EdgeLabelId, EdgeLabelStats> edge_labels;
    std::unordered_map<uint32_t, IndexStats> indexes;
};

struct LabelStats {
    int64_t vertices = 0;
    std::unordered_map<uint16_t, PropertyStats> properties; // null count / NDV / histogram
};

struct EdgeLabelStats {
    int64_t edges = 0;
    double avg_out_degree = 0;
};

struct IndexStats {
    int64_t rows = 0;
    int64_t null_keys = 0;
    bool ready = false;
};
```

- 统计是 **advisory**：CBO 只用它做估算，不参与提交/回滚判定；
- 统计更新不递增 `schema_generation`；
- 快照用 `shared_ptr<const GraphStatsSnapshot>` 发布；旧快照由引用计数保活。

### 2.2 事务 delta

```cpp
struct StatsDelta {
    std::unordered_map<LabelId, int64_t> vertex_delta;
    std::unordered_map<EdgeLabelId, int64_t> edge_delta;
    std::unordered_map<uint32_t, int64_t> index_delta;
    // 可选：property null/存在计数、小 HLL 摘要
};
```

- 写路径只改事务本地 delta；不在热路径做全局 read-modify-write；
- `commit` 成功才合并；`rollback/close` 丢弃；
- 批量写按 chunk 提交，delta 可合并。

## 3. Stats journal（同事务）

### 3.1 表

在 **数据 DB** 建 `table:stats_journal`：

```text
key   = BE64(seq)
value = {version, StatsDelta, txn_id?}
```

- 与用户数据在**同一个 WiredTiger 事务**写入；
- 统计写入失败 ⇒ 整个数据事务失败，不允许“数据提交、统计半写”；
- 提交后把 delta 合并到内存 `GraphStatsStore`；
- journal 保留到下一次 stats snapshot；之后按 `applied_journal_seq` 压缩。

### 3.2 提交顺序

```text
1. 用户数据写入（同一 WT session/txn）
2. 若 stats_delta 非空：写 stats_journal row（同一 txn）
3. commit_transaction
4. 成功：StatsStore::applyCommitted(delta, seq)
5. 失败：事务回滚，journal 和数据都不生效
```

**不要**在 commit 前合并内存统计；失败会产生假统计。

### 3.3 崩溃恢复

- 数据+journal 原子提交；崩溃不会出现“数据有、journal 无”；
- 启动：
  1. 读取 stats snapshot（若有）；
  2. 重放 `applied_journal_seq` 之后的 journal；
  3. 若无 snapshot 或 journal 不完整：标记 `UNKNOWN`，从数据重建；
- journal entry 有唯一 seq；重放由 snapshot offset 保证幂等，不从旧内存继续。

## 4. 内存合并与并发

### 4.1 槽位

- LabelId/EdgeLabelId/index_id 只追加，不复用；
- `GraphStatsStore` 用 append-only 槽位（分块数组/deque）；
- 新槽位用生命周期锁创建；之后读路径无锁。

### 4.2 合并

- 单计数器：`std::atomic<int64_t>::fetch_add`；
- 一组计数器/直方图桶：per-label/edge/index **分片锁**，整组 `+=`；
- 多分片按 ID 升序加锁，避免死锁；
- 不跨 WT commit/IO 持统计锁；
- 全局发布锁只保护 snapshot 指针交换，不保护增量合并。

### 4.3 快照

- 发布新快照时，按固定顺序读槽位/加短锁复制；
- 规划器只持 `shared_ptr<const GraphStatsSnapshot>`，不阻塞写；
- 统计允许跨计数器轻微不一致；若需要严格一致，使用 stats epoch/seqlock。

## 5. 重建

全量重建不能 `counts[id] = scanned` 覆盖并发写。

采用 baseline + delta：

```text
1. 记录 epoch E，取稳定数据快照；
2. 扫描 baseline；
3. E 期间写入已在 journal/delta；
4. 发布 baseline + delta_E，切换 E+1；
5. 压缩 journal 到 snapshot_seq。
```

若重建失败：保留旧快照并标 `STALE`；不发布半成品。

## 6. CBO 集成

### 6.1 逻辑 Join 条件（不是 BoundHashJoin）

给 `BoundBinaryJoinOp` 增加**逻辑谓词**：

```cpp
std::vector<BoundExpression> join_conditions;
```

- Filter 上的可下推等值谓词规范化到 join_conditions；
- 残差 Filter 保留；
- 这是语义/条件，不是 HashJoin 算法。

### 6.2 物理候选

Cascades memo 中产生：

- `ImplHashJoinRule`：有至少一个左右可解析等值 key，构建预算内；
- `ImplIndexJoinRule`：目标侧有可用索引，反向 Expand + Join 更便宜；
- `ImplNestedLoopRule` / `ImplCrossProductRule`：兜底；
- 每条规则返回等价候选，`CostModel` 比较。

### 6.3 ChosenPlan 保留算法

`ChosenPlan.tag` 必须一路保留到物理 lowering：

- HashJoin tag → 构造 `HashJoinPhysicalOp`；
- IndexJoin tag → IndexScan + Expand + Join；
- Enricher 仍按现有逻辑；
- 不允许在 logical plan 上挂“物理算法 hint”。

### 6.4 统计缺失

规划器状态：

```text
UNKNOWN / BUILDING / STALE / APPROX / READY
```

- `UNKNOWN/BUILDING/STALE`：不产生依赖统计的 HashJoin/IndexJoin 候选，保留安全计划；
- `APPROX/READY`：允许代价比较，并记录 confidence/age 到 EXPLAIN。

## 7. 分阶段交付

### Phase 1：StatsStore + StatsDelta + journal 表（基础已落地）

- ✅ `GraphStatsStore` / `GraphStatsSnapshot`（原子槽位、不可变快照）；
- ✅ `GraphSession` stats delta；
- ✅ 顶点/边写路径计数 hook（现有 label 去重）；
- ✅ 同事务 `table:stats_journal` 写入，commit 后合并、rollback 丢弃；
- ✅ 启动重放 journal（无 snapshot；旧数据可能未被 journal 覆盖）；
- ✅ 单测：并发计数、正负 delta、快照不可变、journal/rollback；
- ⏳ 尚未做：snapshot 表、journal 压缩、CBO 读取、重建。

### Phase 2：崩溃恢复/重建

- snapshot 持久化 + journal offset；
- 启动重放/重建；
- `UNKNOWN→READY` 状态；
- kill-after-commit 测试。

### Phase 3：CBO HashJoin 候选（已落地基础版）

- ✅ 逻辑 `join_conditions`（Filter 原谓词保留，作为语义兜底）；
- ✅ `ImplHashJoinRule` + HashJoin cost；
- ✅ ChosenPlan join tag + physical lowering；
- ✅ equi-join 同时有 HashJoin/CrossProduct 候选；CBO 按 local cost + 输入代价比较
  （修复了 any-property winner 漏加 local cost 导致两个算法打平的问题）；
- ✅ 1×3 选 CrossProduct、2×20 选 HashJoin 有单测；
- ⏳ NestedLoop/IndexJoin 候选未接；physical planner 仍保留 HashJoin 代价/预算门禁。

### Phase 4：通用 Join 规则

- ✅ 通用 destination-index join：Filter(Expand) 的 dst 属性索引 → IndexScan(dst)
  → 反向 Expand → HashJoin；条件/索引全部来自 catalog/predicate，无 label 名；
- ✅ 只读 Cross/Inner join commute（写计划/pattern comprehension 走保守边界，
  暂不交换）；
- ✅ 只读 join associativity：Join(Join(A,B),C) → Join(A,Join(B,C))（同一 safe 边界）；
- ⏳ 统计驱动的选择性 join reorder（当前只有 commute/associate 候选）；
- ⏳ SemiJoin/IndexProbe 规则化；
- ⏳ 共享锚点；
- ⏳ 统计 confidence 驱动。

### Phase 5：性能验证（合成基线已完成，LDBC 待做）

- 同二进制、同数据、交错 A/B；
- 结果行一致（全量/哈希）；
- 记录 min/p50，注明轮数/warmup/机器状态；
- 覆盖：小表/大表、选择性高低、索引/无索引、null/重复、LEFT/SEMI；
- **不按查询文本/label 名开关优化**；LDBC 只是 workload 之一。

## 7.5 旧数据与重建决策

- 统计是 advisory：旧图没有 journal/snapshot 时**不强制全量重建**；
- label 计数以 schema 为 baseline，只覆盖 stats 明确拥有的 label（防止部分 stats 把旧 label 估成 0）；
- edge/NDV/索引统计缺失时标记 UNKNOWN/APPROX，CBO 保守回退；
- 全量重建仅用于显式 `ANALYZE`、批量导入绕过 hook、或需要 edge/分布统计的旧图；
- journal 压缩不是当前阶段目标：snapshot 与 applied_seq 尚未严格原子，提前删除有漏计风险。

## 8. 禁止事项

- 不允许用 `labelNameToId("Tag"/"Message")` 或 `edge_name=="HAS_CREATOR"` 产生计划；
- 不允许只优化单个 benchmark 查询；
- 不允许统计更新丢失后靠“再扫一遍但覆盖”修复；
- 不允许把统计写入放在 commit 之后单独事务（除非明确降级为近似且可重建）；
- 不允许用测试/基准期望值反向“证明”优化有效。

## 9. 风险

- journal 写放大：只记必要 delta，批量合并，snapshot 后压缩；
- 统计锁竞争：分片锁/原子，不长持；
- ChosenPlan physical tag 传递是跨层改动，需单独 review；
- 统计 stale 导致计划差：confidence/age + fallback，而不是错误结果。

## 10. HashJoin 合成 A/B 基线

命令（同一二进制、同一数据、交错 warmup + 7 轮）：

```bash
EUGRAPH_TEST_DATA_DIR=/home/dodo/.cache/eugraph-bench \
EUGRAPH_BENCH_N=1000 ./build/release/bench_hash_join
```

结果（2026-11-21，本机，合成 Person/City 等值 join，结果行数一致）：

```text
n=1000 runs=7
hashjoin min=3.21956ms p50=3.27782ms max=3.8148ms
cross    min=189.077ms p50=190.62ms  max=192.163ms
```

- `cross` 通过 `EUGRAPH_HASH_JOIN_MAX_BUILD_ROWS=0` 强制回退；
- benchmark 在每次测量前检查 `EXPLAIN` 确实出现/不出现 HashJoin，防止“计时了别的计划”；
- 这是合成基线，不是 LDBC 最终结论；LDBC/SF1 语义+性能对照仍需按 §Phase 5 执行。

## 11. LDBC SF1 spot check（2026-11-21）

同一 SF1 数据、`eugraph-server`、4 compute / 4 IO / 1GB WT cache、`--wt-txn-sync none`，
`database=sf1`，固定真实 `personId=933`、`tagName=Hamid_Karzai`，2 warmup + 5 iters：

```text
c6  rows=10  min=1852.51ms p50=1862.11ms
c9  rows=20  min=1491.11ms  p50=1520.54ms
```

结论：**通用规则尚未补回旧硬编码计划的性能**。历史上 c6 专用重排在 SF1 约 45.7ms
（见 `docs/benchmark/ldbc-snb-sf1-comparison.md`）；移除硬编码后当前 c6 约 1.86s。
这说明只读 commute/associate 只是搜索空间的一小步，还缺选择性驱动的 anchor/join order、
semi-join/decorrelation 和 destination-index 的 CBO 决策。当前不能把“移除硬编码”宣称为
性能等价完成。
