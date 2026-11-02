# LDBC sf0.1 计划质量对照与剩余瓶颈（2026-11-02）

> 目的：回答“慢是不是因为计划选得不好”。同一份数据、同一参数，EuGraph `PROFILE`
> 与 neo4j `PROFILE` 逐算子对照 rows / dbHits / 访问次数，再用 GDB 采样与查询变体
> A/B 交叉验证。结论：**c5/c6/c9 的计划形状与 neo4j 基本一致，差距在每次访问的
> 成本与标签成员判定；c14 存在真正的计划冗余（两个 PC 各跑一遍相同前缀）**。
> 绝对耗时随 CPU 频率漂移，只看同轮比值。

## 1. 方法与口径

- eugraph Release 8 compute / 4 storage IO / 1GB WT cache / `--wt-txn-sync none`；
  neo4j 5.26.32 本地 8687（1GB heap / 512MB pagecache），数据为同一份 CsvComposite sf0.1。
- `PROFILE` 的 `selfTime/selfCpu`、`dbHits`（WT search + search_near + scan rows）、
  `ioDispatches` 按算子归属；neo4j 看 `dbHits`/rows。
- GDB 原生化协程栈采样（perf 在本机被 `perf_event_paranoid`/容器权限拦住）。
- 查询变体 A/B（去掉某个谓词/标签）用于把“计划里某个动作”的成本单独测出来。
- 本轮优化后的数字（c14 邻居缓存、c6 prefetch 快路径已落地）见
  [ldbc-snb-sf0.1-comparison.md](ldbc-snb-sf0.1-comparison.md) §3.5–§3.7。

## 2. c9：计划形状相同，差在“53k 次属性读 + 286k 行标签扫描”

EuGraph（118 ms）与 neo4j（28 ms）都是：

```text
Person(id) seek → KNOWS*1..2 (174 friends) → Expand HAS_CREATOR (53,192 messages)
  → Filter message.creationDate < maxDate (7,706 rows) → Sort/Top → 20 rows
```

| 项 | EuGraph | neo4j |
|---|---:|---:|
| 消息展开 | 53,192 行，scan 53,192 + **label_fwd_Message 286,744 行** | 53,192 行 / 54,897 dbHits |
| creationDate 过滤 | 53,192 次 coalesce 查找（**64.7 ms**，wtSN 53,192） | 60,898 dbHits（~10 ms） |
| Expand 总计 | 37.6 ms（其中 label 扫描约 20–26 ms） | — |

- **标签成员判定**：`message:Message` 在 edges ≥ 4096 时走
  “扫整张 `label_fwd_Message` + 成员集合”的启发式；变体 A/B（去掉 `:Message`）
  实测该检查 **约 26 ms**。这不是计划选错，而是执行器用一次全表扫描换 53k 次点查。
- **属性读**：coalesce 对每个 message `search_near{vid}{0x01}` 后逐属性行 `next`，
  单条约 1.2–1.3 µs；neo4j 的 node record 内联属性约 0.2 µs。
- 去掉标签检查后 c9 仍约 92 ms，差距主体是 53k 次属性读的**存储布局**。
- 已实测并否证的替代路径：把 coalesce 的“前缀扫描 + 逐属性 next”改成“每个候选一次精确
  search”（同二进制 env 开关，c9/c3 同机交错 A/B）反而 **慢 1.50×**（c9 119→180 ms、
  c3 92→139 ms；c14 +23%、c6 +30%）。4 个候选 = 4 次 B-tree 下降，比 1 次下降 + 少量
  next 更贵；**不要走这条**。

## 3. c6：计划正确，`:Post` 标签检查 13.3 ms

计划与 neo4j 同为 `friends → HAS_CREATOR(53k) → :Post 过滤(17,739) → HAS_TAG(11,538)`。
EuGraph 66 ms / neo4j 26 ms。PROFILE 里 `Expand(f→post)` self 19.5 ms、scan 188,893
（= 53,192 条边 + **label_fwd_Post 135,701 行**）。变体 A/B：

```text
(f)<-[:HAS_CREATOR]-(post:Post)   min 23.20 ms
(f)<-[:HAS_CREATOR]-(post)        min  9.89 ms
```

即标签成员判定 **13.3 ms**。`HAS_TAG` 侧 `dst:Tag` 的点查 17,739 次，另一部分成本。

## 4. c5：89k 次 per-edge seek，计划形状与 neo4j 一致

EuGraph 194 ms / neo4j 86 ms。右侧：

| 动作 | EuGraph | neo4j |
|---|---:|---:|
| forum → CONTAINER_OF → post | 89,415 行，23.0 ms | 89,415 / 89,415 dbHits |
| post → HAS_CREATOR → friend | 89,415 行，**76.7 ms**（89,415 次 search_near） | 89,415 / 89,415 dbHits |
| Filter friend IN friends | 89,415 → 262 行，10.4 ms | 262 行 |
| membership.joinDate eprop | 29,894 次精确 search，41.2 ms | 关系属性内联 |

neo4j 也是 89k 次关系展开，但邻接/属性内联在 node/rel record 里；EuGraph 每次
`search_near` ~1 µs。**不是 join 顺序问题**：手工反向展开曾被证伪（CrossProduct 灾难）。

## 5. c14：真正的计划冗余

优化后 c14 90 ms / neo4j 14.6 ms。剩余（PROFILE）：

- 两个 PC 分支各约 42 ms：Expand1/2/3 合计 ~17–18 ms、coalesce(PE) ~6 ms、
  Filter ~7.5 ms、anchor PE ~5.5 ms。
- 4 条路径 12 条关系、去重后 8 个人；两个 PC 的 `a←HAS_CREATOR←comment→REPLY_OF`
  前缀完全相同，却各自展开一遍。邻居缓存已把**分支内**重复消掉（dbHits 125k→57.7k），
  但**跨分支**仍重复。
- neo4j 的 `COLLECT { ... }` 子查询只对每个 `r` 算一次，两个权重共享同一子查询结果。

可做：语句内共享邻居缓存（跨 Expand 实例，按 (dir, edge labels, dst labels) 分组），
预计再省 ~10–15 ms；彻底做法是把两个 PC 的公共前缀做 CSE/去相关 join，可到 ~50 ms。

## 6. 结论与优先级

| 方向 | 受益查询 | 预计收益 | 风险/成本 |
|---|---|---|---|
| 语句级共享邻居缓存（跨 PC/Expand） | c14 | −10~15 ms | 低-中：共享生命周期与 key |
| 边端点标签约束（loader 声明 + planner 省略冗余 label 检查） | c6/c14/c5 的单一端点 | c6 ~8 ms、c14 ~3 ms | 中：元数据编码兼容、loader/DDL 变更 |
| 标签内联/邻接内联到边记录，或 vertex 单记录（label+props 一行） | c9/c6/c5/c14 | 每 dbHit 成本 3–5× | 高：KV 编码兼容面，需独立设计评审 |
| 读路径安全并行（同快照跨 IO worker） | c9/c3/c5 | 大（IO 墙钟 ÷ N） | 高：快照一致性，需事务/存储设计 |
| c5 半连接/反向展开（friends→posts→forum） | c5 | ~30–40% 的 seek 数 | 高：planner/CBO，历史手工改写已证伪 |

**不要做的事**：继续在算子里做无测量的 cursor/批处理微调；用简单 pattern 重排替代
CBO；给 label 检查加全局缓存（读快照隔离下会返回新于事务快照的标签）。

## 7. 复现

```bash
# 同机 eugraph release + neo4j 8687
python3 scripts/bench_ldbc_ab.py --queries-dir <ldbc queries> \
  --neo-uri bolt://127.0.0.1:8687 --only complex-5,complex-6,complex-9,complex-14

# 变体：去掉 :Message / :Post 测标签检查成本
# PROFILE 树：python3 里跑 PROFILE <query> 后读 summary.metadata['profile']
```
