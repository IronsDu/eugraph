# Pattern comprehension inside a list comprehension: findings and the open defect

Status: the timeout is fixed; the **element count is still wrong** on a real store,
so a query that depends on it (LDBC complex-10's `commonPostCount`) is not
trustworthy yet. This document is the reference for finishing that work.

Everything below was measured on the sf0.1 store with the server restarted before
each measurement (see "Measurement hazards"), and compared against neo4j on the
same data.

---

## 1. The two failure classes

### 1a. Wrong result: a pattern predicate was not applied at all

```
MATCH (p:Person {id:...})<-[:HAS_CREATOR]-(post:Post)
WITH collect(post) AS posts, p
RETURN size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)]) AS c
-- before: 352 (= size(posts), the filter never ran)
```

Cause: the RETURN/WITH lowering decision inspected only the item's **outermost**
node, so a comprehension wrapped in a call -- `size([x IN ... WHERE <pattern>])`,
which is exactly what complex-10 writes -- was never seen, `pc_asts` stayed empty,
and the item fell through to the row-wise evaluator. Confirmed by an instrumented
run printing `lc=0 pc_asts=0` at the decision point.

**Fixed** by descending through call wrappers to find the comprehension
(`findListComprehensionUnderCalls`) and substituting only that node
(`substituteListComprehensionUnderCalls`), so `size(...)` still returns a length
rather than the list.

### 1b. Wrong result on a real store: the comprehension's loop variable is not what it should be

On sf0.1, with the same query text and only the literal format changed per engine
(neo4j stores LDBC ids as strings):

```
eugraph   {n: 352, c_explicit: 6, c_anon: 0}
neo4j     {n: 352, c_explicit: 3, c_anon: 3}
```

and the anonymous form -- the one complex-10 uses -- collapses to 0, so
`commonPostCount` is wrong.

---

## 2. Root cause of 1b: the loop variable name collides

The discriminating measurement:

```
RETURN size([x IN posts WHERE <pattern>]) AS s1,
       size([x IN posts WHERE <pattern>]) AS s2          -> {s1: 6, s2: 0}
RETURN size([x IN posts WHERE <pattern>]) AS s1,
       size([y IN posts WHERE <pattern>]) AS s2          -> {s1: 6, s2: 6}
-- neo4j gives 6 and 6 for both forms
```

Renaming only the second comprehension's loop variable makes it correct, so the two
comprehensions are semantically independent and the defect is the shared name `x`.

Mechanism: the lowering creates `BoundUnwindOp` with `variable = lc.variable` and
registers it in the context, while a nested pattern comprehension's pattern
variable is also `x` in the common `[x IN posts WHERE (x)-[:R]->(:Y)]` shape.
Planning resolves by name in several places -- `makeSlotLayout` through
`ctx.var_slots`, correlation through `output_schema[pos] != corr.left_var`, and
`bindExistsSubPlan` through `ctx_.lookup(start_var_name)` / `saved_ctx.symbols` --
so the second comprehension's reference to the list resolves into the first one's
Unwind scope. It is then driven per element and handed a vertex instead of the list.

This one cause explains every failure shape that earlier rounds blamed on something
else: two comprehensions over one list returning 0, the nested
`[x IN nodes(p) | size([(x)-->(:Y) | 1])]` case, and complex-10's `c_anon` being 0
with an inflated `c_explicit`.

### Retired explanations (do not re-investigate)

| Blamed earlier | Why it is not the cause |
|---|---|
| Deduplication / element-count semantics | Dedicated probes (one post/one tag, one post/two tags, three posts with overlapping tags, parallel edges, six posts covering six tag configurations) all match neo4j exactly |
| `existence_only` mis-flagged | If it applied, `size()` could only be 0 or 1; it returned 352, then 6 |
| Cache-corrupting hash collision | `ValueHash` collides, but a `std::unordered_set` still compares on equality |
| Slot mis-resolution | Plan-time resolution is correct: `[RES-APPLY] var=posts slot=4 left_column=1 -> pos=2`, and `schema[2]` is `posts` |
| "The second comprehension reads the original list" | It is driven with a single vertex, not the list |
| `need_entire` / property-list narrowing | Measured flat |

---

## 3. The timeout (fixed)

A variant that previously timed out by more than 400 s ran in 0.44 s once fixed, and
its plan lost both `AllNodeScan` and `CrossProduct`. The mechanism:

```
before   CorrelatedSource -\                       after   CorrelatedSource
                           >- CrossProduct                  -> Expand(src=p, ...)
         AllNodeScan(p) --/                                -> Expand -> person
```

`bindExistsSubPlan` only records a correlation pair when the pattern's start
variable resolves in the current scope, and the list lowering restores its saved
context before returning, so a comprehension sitting in its own `WITH` layer was
bound after that restore and its free variable no longer resolved. The planner then
had no way to know it was the list element and fell back to a full scan joined with
the correlated source -- a cartesian product of every node in the store with each
row of the group. That is the whole of the ~180x, and it also explains why a small
synthetic graph looked unaffected: what scales is the store's node count.

**Fixed** by capturing each lowered loop variable's binding before the lowering
discards it, re-registering it around the hoisting pass that binds the inner
pattern comprehension, and erasing it afterwards.

Note this fixed the **timeout only**, not 1b.

---

## 4. Why the obvious fix -- rename the loop variable -- does not work

Three attempts, each blocked further in:

1. renaming at the clone sites broke the pointer-keyed patch map;
2. a shadowing-aware walker was needed, because `[x IN posts | size([(x)-->(:Y) | 1])]`
   has an outer `x` and an inner `x` and renaming the inner one breaks it;
3. carrying the rename through the lowering (unique `__lc_loop_N` for the Unwind
   plus an `outer_vars` filter dropping names only a nested comprehension binds)
   still fails `QueryExecutorTest.PatternComprehensionInsideListComprehension`
   with `PatternComprehensionApply: empty correlation is unsupported`.

The blocker is structural. That test is
`RETURN n.n, [x IN nodes(p) | size([(x)-->(:Y) | 1])]`: the **outer comprehension's
list expression contains a pattern comprehension** whose pattern variable is the
outer loop variable `x`. In the working baseline the outer lowering registers `x`,
so the nested comprehension finds it and correlates to the element column. Renaming
severs exactly that link.

So the name is **load-bearing across a nesting boundary** while being **harmful
across sibling comprehensions**:

* siblings over one list must NOT share the name (the `{6, 0}` defect);
* a nested pattern using the name MUST resolve it to the enclosing element.

A name-level workaround cannot satisfy both.

---

## 5. The fix to implement: stop resolving this by name

The repository's binder identity rule already asks for this:

> `VariableId = SlotId`. Semantic identity is the binding slot the Binder
> allocated; using a variable name or `ScopeId + name` as semantic identity is
> forbidden.

The comprehension lowering is a place where that rule is not yet honoured:

* `bindExistsSubPlan` decides correlation by looking the start variable up by name
  (`ctx_.lookup(start_var_name)`, `saved_ctx.symbols`);
* the Apply's correlation resolution cross-checks by name
  (`output_schema[pos] != corr.left_var`);
* the lowering registers its loop variable by name in `ctx_.symbols`.

Carrying the SlotId through those three paths, instead of the name, separates the
sibling case from the nesting case naturally: siblings have different slots, while
a nested pattern that refers to the enclosing element resolves to the same slot.

Acceptance criteria for that work:

* the discriminating case must go from `{6, 0}` to `{6, 6}`, and the labelled and
  anonymous forms must agree with neo4j (`3` and `3` for person 933);
* `query_executor_tests` stays at 521/521, `optimizer_tests` at 109/109;
* TCK unchanged;
* LDBC complex-10 runs without a timeout **and** returns the same
  `commonPostCount` as an independent route (see below).

A regression test for the sibling case should be added when it passes:
`RETURN size([x IN l WHERE <pattern>]) AS a, size([x IN l WHERE <pattern>]) AS b`.

---

## 6. Verification recipes

### Independent recomputation of complex-10's counts

`commonInterestScore = 2*cpc - pc`, so a self-consistency check catches an obviously
wrong `cpc`. For a real cross-check use a second execution path rather than the
comprehension, e.g.

```
OPTIONAL MATCH (person)-[:HAS_INTEREST]->(t)<-[:HAS_TAG]-(q:Post)<-[:HAS_CREATOR]-(friend)
RETURN count(DISTINCT q)
```

and compare per friend against the comprehension's value. When writing such
queries, do not leave an unbound node pattern such as `(city:City)` in scope: it
multiplies the row count and produces nonsense (56 406 posts instead of 42 were
observed that way).

### Isolated probe instance

The benchmark stores cannot be used for probes: they hold the sf0.1 import, the
test suite writes `:X`/`:Y`/`:L` into whatever instance is running, and
`MATCH (n) DETACH DELETE n` on them exceeds neo4j's transaction memory limit (and
hangs eugraph). Use a standalone neo4j whose database starts empty, so every probe
begins from a genuinely fresh graph:

```
mkdir -p /tmp/n4j-probe/{data,logs,run,conf}
# conf/neo4j.conf: bolt 7699, http 7499, https off, auth off,
#                  data/logs/run under /tmp/n4j-probe, small heap+pagecache
NEO4J_CONF=/tmp/n4j-probe/conf neo4j-admin dbms set-initial-password probepassword123
NEO4J_CONF=/tmp/n4j-probe/conf nohup neo4j console > /tmp/n4j-probe/console.log 2>&1 &
```

---

## 7. Measurement hazards

Two mistakes produced readings that looked like engine differences but were query
errors. Both cost several rounds:

* **different data on the two engines.** Probe both sides on the same graph;
* **inconsistent literal formats.** neo4j stores the LDBC ids as strings, so
  `{id:933}` matches nothing and returns 0 rows, which reads as "eugraph has rows,
  neo4j has none".

Two more:

* **server state.** The same query measured 0.18-0.44 s on a freshly started server
  and timed out on one that had already served a hung query; a run of timeouts left
  even simple controls hanging. Restart the server before each measurement.
* **operator identity in traces.** Tracing the Apply's injected values produced
  lines from several operators at once; without printing the operator's `this`
  pointer they look like one confused operator, and one earlier conclusion ("the
  second comprehension reads col1") came from exactly that conflation.

## 更正：真实失败模式是 AllNodeScan，不是「计数偏大」

在**本轮干净重导入**的数据（与原始 CSV 逐行核对一致）上复测，得到与本文档此前记录**不同**的症状：

```
MATCH (p:Person {id:933})<-[:HAS_CREATOR]-(post:Post)
WITH collect(post) AS posts, p
RETURN size(posts) AS n,
       size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)]) AS c_anon,
       size([x IN posts WHERE (x)-[:HAS_TAG]->(:Tag)<-[:HAS_INTEREST]-(p)]) AS c_tag
-- eugraph {n: 352, c_anon: 352, c_tag: 0}
-- neo4j   {n: 352, c_anon: 3,   c_tag: 3}
```

`c_anon` 等于 `size(posts)`，即**内层谓词根本没有约束住元素**；而本文档此前记录的是 `c_anon=6`、`c_tag=6`。
旧数据目录（`eugraph-sf0.1-cli-data`）上现在同样得到 `352 / 0`，说明**此前那批读数（6 与 c_tag=6）来自当时被实验残留污染的数据** ——
本文档已记录过探针会把 `:X`/`:Y`/`:L` 写进运行中的实例，那批数正是这个问题的产物。

**同时被推翻的还有「循环变量名冲突」这一根因。** 判别用例在干净数据上复测：

```
[x IN posts WHERE <pattern>] AS l1, [y IN posts WHERE <pattern>] AS l2   ->  l1=len352, l2=len0
```

异名同样失败（此前记录是「改名即可修复」）。所以名字冲突不是当前代码在真实数据上的病因。

## 真实的根因（计划层，已确认）

`EUGRAPH_PLAN_DEBUG` 显示这条查询**确实走了** `PatternComprehensionApply`，但内层子计划是：

```
CrossProduct |slots: 7 5 |
  CorrelatedSource |slots: 7 |
  AllNodeScan(variable=x) |slots: 5 |        <- 内层 x 变成全图扫描
    Expand(src=x, dst=__anon_2, labels=[14], direction=OUT)
      Expand(src=__anon_2, dst=__exists_dst_1, labels=[15], direction=IN)
        Filter
          Filter
```

**内层循环变量 `x` 被当作「需要外部关联的变量」解析，而不是由 `Unwind` 产生**，于是规划器退化为
`AllNodeScan(x)`（全库 286,744 个 Message）× 关联源的笛卡尔积，行数随之错误 —— 这正是 `size` 得到 352 的原因，
也是同一个笛卡尔积在两轮之前导致 >400s 超时的那个模式。

定位到具体位置：`bindExistsSubPlan`（`src/query/planner/binder/bind_match.cpp`）在约 1126 行按**名字**在
`ctx_` 中查找模式的起始变量：

```cpp
if (auto* outer_col = ctx_.lookup(start_var_name)) {   // start_var_name == "x"
    is_correlated = true;                              // 找到了 → 走关联路径
    outer_slot = outer_col->slot_id;                   // 而这个 x 属于外层推导
```

对 `[x IN posts | size([(x)-->(:Y) | 1])]` 这类嵌套，内层推导的模式变量与外层推导的循环变量同名，
按名查找命中的是**外层**的绑定，于是内层被错误地当作关联子计划（计划中的
`__exists_saved_1` / `__exists_dst_1` 就是这条路径的产物），而它真正需要的 `x` 来自自身的模式。

## 下一步

在 `bindExistsSubPlan` 判定关联之前，先判断起始变量是否是**本模式自己引入**的（模式里的节点/边变量），
是则不作为外部关联；只有引用外层作用域的变量才建立关联对。这与本文档 §5 的 SlotId 方向一致
（语义身份应取 SlotId 而非名字），但**症状与修法都比 §5 描述的更具体**：问题不在「兄弟推导共用一个列表时名字相撞」，
而在「嵌套推导的模式变量被误解析为外层绑定」。

## 最终根因：推导缺少「按元素去重」

在干净数据上，两引擎的**底层模式匹配完全一致**：

```
MATCH (p:Person {id:933})<-[:HAS_CREATOR]-(post:Post)
MATCH (post)-[:HAS_TAG]->(t:Tag)<-[:HAS_INTEREST]-(p)
RETURN count(*) AS pairs, count(DISTINCT post) AS distinct_posts
-- eugraph {pairs: 16, distinct_posts: 3}
-- neo4j   {pairs: 16, distinct_posts: 3}
```

而同一谓词写成列表推导时：

```
RETURN size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)])
-- eugraph 6      neo4j 3
```

**6 = 16 对中的 6 行匹配数（每帖匹配 2 次），3 = 去重后的元素数。** 所以缺陷是：
**推导按「(元素, 匹配) 对」逐个产出元素，而没有像 Cypher 语义那样「每个元素最多保留一次」。**

这一条同时解释了：

* 为什么 `c_tag=0`（显式 `:Tag` 时走的是另一条路径，元素未被产出）；
* 为什么 `[x IN …] AS l1, [y IN …] AS l2` 里 `l2=len0`（第二个推导的 Apply 被驱动方式不同）；
* 为什么 complex-10 的 `commonPostCount` 偏大 —— 它与本例同源。

**与之前两个假设都无关**：不是循环变量名冲突（异名同样失败），也不是「SlotId 未传递」
（关联已正确建立：计划为 `CorrelatedSource → Unwind(x) → (x)-[:HAS_TAG]->()←[:HAS_INTEREST]-(p)`，
无笛卡尔积、无 AllNodeScan）。

## 修法

降级计划在 `collect` 之前需要一次**按循环变量去重**：

```
Unwind(x) → Expand(x -HAS_TAG-> __anon_2) → Expand(__anon_2 <-HAS_INTEREST- p)
  → [Project(x)] → [DISTINCT on x] → Aggregate(collect)
```

要点：

* 去重键是**循环变量列**（只此一列），不是整行 —— 子计划还带关联列（`__exists_saved_1` 等），
  `BoundDistinctOp` 默认是整行比较，用它会折不掉任何东西（本文档 §「Keyed DISTINCT」已记录该次失败尝试）；
* 因此 `BoundDistinctOp::key_columns` 这个能力正是为此准备的：按列名解析到子计划输出中的元素列，
  只对该列去重（能力已在 main 中，但当前**无调用者**）；
* 注意 `WITH DISTINCT`（用户显式写的）走的是另一条路径，不要混淆。

验收：`size([...])` = 3（与 Neo4j 一致）、`[x IN …]` 长度为 3、complex-10 的 `commonPostCount`
与独立路径 `OPTIONAL MATCH (person)-[:HAS_INTEREST]->(t)<-[:HAS_TAG]-(q:Post)<-[:HAS_CREATOR]-(friend) RETURN count(DISTINCT q)`
一致；521/521 与 TCK 不变。

## 去重的插入位置（两次实测失败，位置已确定）

按「在 `collect` 之前按循环变量去重」实现了 `BoundDistinctOp::key_columns`（按**列名**解析）、
规划器解析、优化器保字段，并把 `DISTINCT` 插在降级里。两次插入位置都错，**结果完全不变**（`c_anon` 仍 6/43/219）：

| 插入点 | 计划中的实际位置 | 结果 |
|---|---|---|
| 在 `apply->left = std::move(child)` 之前 | `Distinct |slots: 4 5 10 |` 位于 **Apply 之上**（外层 child） | 无变化 |
| 「`Unwind` + `where` filter 之后」 | 仍在 `Aggregate(keys=0)` **之上**，`Filter`/`Apply` 之下 | 无变化 |

关键观察：计划里外层是

```
Aggregate(keys=0)          <- 外层 collect
  Distinct |slots: 4 5 10 |  <- 插入的，键列 10 是「列表」而不是元素
    Filter
      PatternComprehensionApply
        Unwind |slots: 4 5 |   <- x 在这里（slot 5）
```

`Distinct` 落在 `Aggregate` **之上**，它面对的行已经是「每行一个列表」，键 `x` 解析不到 → 退化为整行去重 → 折不掉任何东西。

**正确位置是 `Aggregate(collect)` 内部、`Unwind` 之后**，即子计划变成：

```
Aggregate(keys=0, aggs=1)          <- 内层 collect
  Project(items=[__pc_proj])
    Filter                          <- WHERE
      Distinct(key = <循环变量>)     <- 插在这里
        Expand(...)                 <- 模式匹配链
          Unwind(x)
            CorrelatedSource
```

代码里的难点：降级函数中 `current` 在 `apply->right = std::move(current)`（约 1262 行）时被移进 Apply，
而内层 `Aggregate(collect)` 是在那之后（约 1240-1249 行）才 `current = std::move(agg)` 的。
所以插入点必须在**构造 `Aggregate` 之前、`where`/`projection` 绑定之后**，且作用对象是那时的 `current`
（= `Unwind` + `Filter`），而不是之后的 `agg`。

下一轮只需改这一处（约 3 行），随后跑 521 + Neo4j 交叉验证即可。

## 真正的根因：推导丢弃了「第二跳」的约束

在干净库上用**引擎自身的基础查询**做权威对照，问题定位得非常简单：

```
MATCH (p:Person {id:933})<-[:HAS_CREATOR]-(post:Post)-[:HAS_TAG]->(:Tag)                    -> 6   (单跳)
MATCH (p:Person {id:933})<-[:HAS_CREATOR]-(post:Post)-[:HAS_TAG]->(:Tag)<-[:HAS_INTEREST]-(p) -> 3   (两跳)
```

而列表推导 `size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)])` 返回 **6** ——
等于**单跳**的结果，即 **第二跳 `<-[:HAS_INTEREST]-(p)` 的约束没有生效**。

逐帖核对（p933 的 6 个带 tag 的帖子）：

| post | 其 tag | 与 p933 的共同 tag |
|---|---|---|
| 3 | … | 6 |
| 1030792151044 | … | 6 |
| 618475290624 | [59, 779, 3047, 5159] | 4 |
| **412316860440** | [1930, 7196, 10890] | **0** |
| **962072795511** | [6429] | **0** |
| **962072795586** | [584, 1526, …, 11997] | **0** |

推导返回全部 6 个，把 3 个「共同 tag = 0」的帖子也算了进去。

**关键对照**：把谓词写成**单跳** `(x)-[:HAS_TAG]->()`，推导返回的也是**同样 6 个** ——
证明谓词被降级成了单跳。而把 `:Tag` 写成显式标签、或去掉 `p`，结果依然是同样的 6 个，
所以与匿名节点、与变量命名都无关。

计划里两跳 Expand **都在**（`labels=[14]`、`labels=[15]`），所以问题出在**第二跳 Expand
是否真的按绑定端点过滤**，而不是计划缺了算子。

## 这推翻了此前所有关于「关联」的假设

* 不是循环变量名冲突（异名同样返回 6）；
* 不是 SlotId 未传递（关联已建立：计划为 `CorrelatedSource → Unwind(x) → 两跳 Expand`）；
* 不是「缺少按元素去重」（导出的 6 个元素**互不相同**，去重是 no-op）。

**同样的机制解释了 complex-10 的 `commonPostCount` 偏大。**

## 缺陷隔离的最终证据（同一库、同一模式、逐步对照）

在干净库上，用**引擎自身的基础查询**与推导逐项对照同一模式
`(x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)`：

| 方式 | 结果 |
|---|---|
| 基础 `MATCH (p {933})<-[:HAS_CREATOR]-(x:Post)-[:HAS_TAG]->(t:Tag)<-[:HAS_INTEREST]-(p)` | **3** 个 post（3、618475290624、1030792151044） |
| 逐帖 `OPTIONAL MATCH` 计数 | 349 个为 0，仅上述 3 个非 0（6 / 4 / 6） |
| 推导 `[x IN posts WHERE 同模式 \| x.id]` | **6** 个 post |

多出的 3 个：`412316860440`、`962072795511`、`962072795586` —— 逐帖验证它们的
`HAS_TAG` 标签与 p933 的兴趣**交集为 0**，即**不应命中**。

**已排除的解释**（每项都实测）：

| 假设 | 反证 |
|---|---|
| 循环变量名冲突 | 异名 `y` 返回同样的 6 |
| SlotId 未传递 / 未关联 | 计划为 `CorrelatedSource → Unwind(x) → 两跳 Expand`，关联已建立 |
| 缺少按元素去重 | 导出的 6 个元素互不相同，去重是 no-op |
| 缺算子 | 两跳 Expand 都在计划里（`labels=[14]`、`labels=[15]`） |
| 未知关系类型不被强制 | 换成 `ZZNOTEXIST` 返回 **0**，说明**类型是强制的** |

所以约束并非「完全不生效」，而是**第二跳对部分元素失效** —— 这是一处需要继续定位的
算子/求值层缺陷，`PatternComprehensionApplyPhysicalOp` 的运行期行为是首要检查点
（注意它的 `existence_only_` 分支会**刻意发出一个占位元素**，设计目的是仅用于
`size(list) > 0`；若该标志在本场景被误置，就会把「无匹配」的元素变成「有匹配」）。

**这是 complex-10 `commonPostCount` 偏大的同一根因。**

## 触发条件已钉死：`p` 作为关联变量进入子计划

判别矩阵（同一库、同一模式 `(x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(终点)`）：

| # | 关联变量 | 第二跳终点 | 结果 |
|---|---|---|---|
| T1 | 无 | `(:Person {id:933})` 常量 | **3** ✅ |
| T2 | 仅 `posts` | `(:Person {id:933})` 常量 | **3** ✅ |
| B | `posts` | `(:Person {id:933})` 常量 | **3** ✅ |
| **T3** | **`posts` + `p`** | **`(p)` 关联变量** | **6** ❌ |
| a | `posts` + `p` | `(p)` | 6 |
| b | `posts` + `p` | `(zz)` 未绑定变量 | 6 |
| c | `posts` + `p` | 常量 | 3 |

**规律**：当 `WITH … , p` 使 **`p` 成为推导的关联变量**时，第二跳的终点约束失效；
只要终点写成常量（不参与关联），结果就正确。

补充观察：单跳 `(x)-[:HAS_TAG]->()` 在同一关联集合下也返回 **6**，与两跳相同 ——
说明一旦 `p` 进入关联，**整个谓词被降级为不含终点约束的形式**。

**这是 complex-10 `commonPostCount` 偏大与 `c_anon=6`（正确为 3）的同一根因。**
定位下一步：`bindExistsSubPlan` 中 `saved_chain_corrs` / `chain_var_rewrite` 的登记 ——
计划里第二跳是 `Expand(src=__anon_2, dst=p, ...)`，而按设计应重写为 `__exists_dst_N`
并由 `Filter(dst == __exists_saved_N)` 约束；`p` 显然没有被登记进 `saved_chain_corrs`。

## 实现进展：链终点检测的范围漏洞已定位（未完成）

`bindExistsSubPlan` 里链终点的登记循环**只遍历中间节点**：

```cpp
for (const auto& pp : exp_patterns)
    for (auto& [rel_pat, node_pat] : pp.element.chain)   // chain = 仅中间节点
        ... node_pat.variable ...
```

而 AST 是**旋转存储**的：`pp.element.node` 是**写法的末尾节点**，`pp.element.chain` 才是中间节点。
所以 `(x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)` 的**终点 `p` 落在 `pp.element.node`**，
从未被登记 → Expand 直接写进 `p`（计划里为 `Expand(src=__anon_2, dst=p, ...)`），
而设计上应改写为 `__exists_dst_N` 并由 `Filter(dst == __exists_saved_N)` 约束 ——
**这就是第二跳约束被丢弃、`size` 得 6（正确 3）的直接原因。**

**当前卡点**：把终点纳入检测（`handle_chain_node(pp.element.node.variable, ...)`）后，
插桩显示 `p` **被判定为候选但未登记**，即 `saved_ctx.symbols.find("p")` 未命中：

```
[CHAIN] consider x (start=x)
[CHAIN] consider p (start=x)      <- 候选，但下一行的登记未发生
```

且计划仍为 `dst=p`。说明这条路径上 `p` 不在 `saved_ctx` 里 ——
需要继续查清单个推导（`lowerListComprehensionWithPatternComprehension`）构造内层
模式推导时，其 `saved_ctx` 是何时捕获的、`p` 是否在那一刻可见。

两条候选修法（都**未验证**，代码已撤回）：

1. 让终点登记**不依赖 `saved_ctx`**，而是像 `extra_corr_vars` 那样用
   `ctx_.lookupBinding(name)` 回退解析（`p` 在更外层作用域可见）；
2. 或在推导降级时就把 `p` 的绑定显式带入内层子计划的作用域，使 `saved_ctx` 能看到它。

**重要**：中途我一度以为「第二跳约束完全失效」，但对照实验否证了它 ——
```
(x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(:Person {id:933})   -> 3  正确
(x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)                  -> 6  错误
```
约束对**常量终点**生效、对**变量终点**失效，与上面「终点未登记」的解释一致。

## 已修复：模式变量未进入关联集合（主形式正确）

**根因**：`collectAllVariables` 只处理 `Variable` 表达式，**不处理 `ExistsExpr`**，而裸模式谓词
`(x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)` 的终点 `p` 是**模式变量**（不是 `Variable` 节点）。
于是 `p` 从不进入 `outer_vars` → 不进子计划作用域 → 第二跳的终点约束被静默丢弃。

插桩证据链：

```
[LCIN] 降级入口:      ctx=[ posts p]        ← p 在上下文中可见
[OV]   outer_vars=[ posts]                  ← 但 p 未被收集！
[CTX]  子计划构建:     ctx=[ x posts]        ← 因此子计划里没有 p
计划:                 Expand(src=__anon_2, dst=p, ...)   ← 缺少 Filter(dst == saved)
```

**修复**：让 `collectAllVariables` 一并收集**模式绑定的变量**（`ExistsExpr` 与嵌套
`PatternComprehension` 的 node/rel 变量）。共 39 行、单文件 `bind_return.cpp`。

**验证**（干净库，与 Neo4j 对照，personId 933/1242/2199023256816）：

| | n | `size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)])` |
|---|---:|---:|
| eugraph | 352 / 354 / 473 | **3 / 1 / 183** |
| neo4j | 352 / 354 / 473 | **3 / 1 / 183** ✅ |

修复前为 352 / 43 / 219 —— 现在与 Neo4j 逐参数一致。`query_executor_tests` 521/521、
`optimizer_tests` 109/109、格式通过。

## 残差：同一 RETURN 中两个模式推导时第二个为 0

修复主形式后仍存在一个**独立**缺陷：同一 RETURN 里出现两个模式推导时，**第二个返回 0**。

```
RETURN size([x IN posts WHERE <pattern>]) AS a, size([x IN posts WHERE <pattern>]) AS b  -> {a: 3, b: 0}
RETURN <同样两个推导，交换顺序>                                                            -> {b: 3, a: 0}
RETURN size(posts), size([x IN posts WHERE <pattern>])                                    -> 第一个 352、第二个正确
```

即「**两个推导共存时，后者为 0**」，与顺序无关、与是否同形无关。

运行期插桩：第一个 Apply 的关联读取 `col2=list[352]`（正确，n_left=1）；
第二个 Apply 的内层被驱动 **352 次**、每行读取 `col3=v8`/`col0=v8`（单个顶点而非列表），
因此其 `Unwind` 得不到列表 → 收集为空。

计划里两个 Apply 也是嵌套的（第二个叠在第一个之上），而第二个的关联列解析
为 `[0, 2]`（`p`、`posts`），指向的是**原始 `posts`**，不是第一个推导的输出 `__lc_6`。
这与「第二个推导应当独立读取原始 `posts`」的语义并不矛盾，所以问题更可能在于
**内层 Apply 的挂靠层级**：它被放在 `Unwind` 之上，于是按行驱动（352 次）而不是每组一次。

**complex-10 现已正确**（此前判断有误，此处更正）。

它写作 `WITH friend, city, size(posts) AS pc, size([...]) AS cpc`，两个 size 共存 ——
按上面的规律这属于**能工作的组合**（推导 + `size(非推导)`）。实测确认：

```
complex-10 返回 10 行；对全部 10 行，用**完全不含推导**的独立路径
  size([(person)-[:HAS_INTEREST]->(t:Tag)<-[:HAS_TAG]-(q:Post)<-[:HAS_CREATOR]-(friend) | 1])
重算 cpc 与 pc，再按 score = 2*cpc - pc 计算：
  10 / 10 行与查询输出一致
```

先前看到的 `cpc = 0` 是**真实数据**（那几位 friend 确实没有共同兴趣帖），不是缺陷 ——
我一度把它当成缺陷，是因为没有做这次独立路径对照。

所以本文件记录的正确性缺陷已修复：`size([x IN posts WHERE <两跳模式>])` 在 933 / 1242 /
2199023256816 上给出 3 / 1 / 183，与 neo4j 一致，complex-10 的分数也与独立路径一致。

## 决定性证据：受控 fixture + sf0.1 交叉复核（complex-10 仍不正确）

我此前两次给出「complex-10 已正确」的结论，**都是错的**。原因是验证方法有缺陷：
① 第一次用 `size([(...) | 1])` 当「独立路径」，那本身就是**模式推导**，与被测机制同源；
② 第二次用普通 `MATCH` 对照确实独立，但 sf0.1 上那批 friend 的 `cpc` 恰好全为 0，
   于是「全部一致」是**平凡成立**，没有真正检验到计算。

### 受控 fixture（两引擎同数据、同查询）

脚本：`/tmp/probe/c10.py`（自建 `:FPerson/:FTag/:FPost`，三个 friend 覆盖「有共同兴趣」
「无共同兴趣」「无 post」三种情形）

| 用例 | eugraph | neo4j | |
|---|---|---|---|
| C1 两跳，**不引用关联变量** | `{2:2}, {3:1}` | 同 | ✅ |
| **C2 两跳，引用关联变量 `person`** | `{2:0}, {3:0}` | `{2:1}, {3:1}` | ❌ |
| C3 两跳，**常量终点** `(:FPerson {pid:1})` | `{2:1}, {3:1}` | 同 | ✅ |
| **C4 complex-10 形状（`pc` + `cpc`）** | `{2: pc2 cpc0}, {3: pc1 cpc0}` | `{2: pc2 cpc1}, {3: pc1 cpc1}` | ❌ |

**规律：子计划中只要引用关联变量（`person`），结果就错；换成常量或不引用就正确。**

### sf0.1 交叉复核（推翻此前的「已正确」）

```
普通 MATCH：MATCH (person:Person {id:933})-[:HAS_INTEREST]->(t:Tag)<-[:HAS_TAG]-(q:Post)<-[:HAS_CREATOR]-(friend)
            WHERE NOT friend=person
            RETURN count(DISTINCT friend), count(DISTINCT q)
  -> {friends_nonzero: 0, posts: 0}

模式推导：  size([p IN posts WHERE (p)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(person)])
  -> 93 个 friend、合计 381
```

逐个复核推导声称 `cpc > 0` 的 friend（用基础 `MATCH`）：

| friend id | 推导 | 基础 MATCH |
|---|---:|---:|
| 24189255811663 | 30 | **0** |
| 6597069767242 | 23 | **0** |
| 2199023256277 | 20 | **0** |
| 1274 | 18 | **0** |

**推导把「无共同兴趣」的 friend 也算成有共同兴趣**。complex-10 输出的分数全部 ≤ 0，
与推导声称的 381 自相矛盾，进一步印证推导路径错误。

### 已确定的机制（尚未修复）

* 关联变量**已被正确收集**：`outer_vars=[ person posts]`；
* `CorrelatedSource` **已正确注册**：`person slot=1 col=0`、`posts slot=8 col=1`；
* 链终点**已正确改写**：计划为 `Expand(src=__anon_3, dst=__exists_dst_1, ...)`；
* 等式 Filter **已正确生成**：`dst=__exists_dst_1(col1) == saved=__exists_saved_1(col2)`；
* **但运行期比较不通过**（`s=0`），而把终点换成常量则通过（`s=1`）。

即：**子计划里关联变量的「读取」环节有问题** —— 结构全部正确，值对不上。
下一步应在 `CorrelatedSourcePhysicalOp` 的取值与 `__exists_saved_*` 的列映射处观测实际值
（本次已在插桩层面逼近，但未完成）。

### 单组场景是好的

同一机制在**单组**（不按 friend 分组）下正确：`size(posts)` 与推导都得到与 neo4j 一致的结果
（personId 933/1242/2199023256816 上 3/1/183）。所以缺陷与「分组」相关，而非普遍失效。

## 观测结果：关联值注入正确，但子计划内的等式 Filter 全部拒绝

对受控 fixture 的 C2（两跳·变量终点）逐层插桩，得到的关键事实：

**1) `CorrelatedSource` 注入的值是正确的**

```
[CS] col0 = VertexRef(id=360451)   <- friend
[CS] col1 = VertexRef(id=360449)   <- person（恒定）
[CS] col0 = VertexRef(id=360452)   <- friend
[CS] col1 = VertexRef(id=360449)   <- person（恒定）
[CS] col0 = idx8                   <- person 本体
[CS] col1 = List[2]                <- posts
```

`person` 始终是同一个顶点（360449），`posts` 是列表 —— 注入环节无误。

**2) 等式 Filter 的每次比较都返回 false，且两侧类型一致**

```
[EQ] left=VVal(360451) right=VVal(360449) -> false    <- friend 2 的路径到不了 person 1
[EQ] left=VVal(360452) right=VVal(360449) -> false    <- friend 3 的路径也到不了
[EQ] left=i64(360449)  right=i64(1540104) -> false
[EQ] left=i64(360449)  right=i64(1540106) -> false
```

类型组合统计：`i64 vs i64` 8 次、`VVal vs VVal` 2 次 —— **不存在跨类型比较**，
所以**不是** `valueEquals` 的「变体索引不同即不相等」问题（那条曾是我的主要嫌疑，此处排除）。

`VVal(360451) vs VVal(360449)` 这两行说明：**第二跳 Expand 到达的顶点不是 person**。
在 fixture 中 `FINT` 只有一条 `person1 -> t1`，而 post n:1 的 tag 是 t1，
所以 `(x)-[:FTAG]->()<-[:FINT]-(person)` 应当命中 x=n:1、终点=person1。
实际终点却是 360451/360452（=两个 friend 的顶点 id），
即 **第二跳落到了 `friend` 而不是 `person`** —— 端点绑定错位。

**3) 待查的下一步**

`Expand` 的 `dst=__exists_dst_1` 与 `Filter(dst == __exists_saved_1)` 结构都在，
但终点落到了错误的顶点。应在 `ExpandPhysicalOp` 的 `dst` 列写入处核对：
它写入的是展开得到的邻居，而 `__exists_dst_1` 的 slot/col 是否与 Filter 读取的一致。
本次观测已定位到「比较两侧类型正确、值指向错误顶点」，未再深入。

## 结论

complex-10 **仍不正确**。受控 fixture（`/tmp/probe/c10.py`）可稳定复现：
C1/C3 正确、C2/C4 错误，触发条件是**子计划引用关联变量**。

## 数据一致性核验（回答「两引擎数据是否一致」）

逐标签、逐边类型对照 `eugraph-sf0.1-fresh` 与本机 neo4j：

| 类别 | 结果 |
|---|---|
| 核心标签 Person / Forum / Post / Comment / Tag / TagClass / Place / Organisation | **8/8 逐项一致** |
| 全部 15 种边类型 | **15/15 逐项一致** |
| Message / City / Country / Continent / University / Company | eugraph 有、neo4j 为 **0** |

后 6 个是**同一批点上追加的细分标签**，不是额外的点：
`Message = Post + Comment = 286744`、`City+Country+Continent = 1460 = Place`、
`University+Company = 7955 = Organisation`。neo4j community 的导入未写这些细分标签，
但点与边都在。**因此按主标签查询时两引擎数据等价。**

查询相关邻域也已核对（person 933）：

| 检查项 | eugraph | neo4j |
|---|---|---|
| KNOWS 邻居 | 3 | 3 |
| 兴趣 tag | 70 | 70 |
| Post / Comment | 352 / 60 | 352 / 60 |
| 2 跳 friend | 171 | 171 |
| **兴趣 ∩ 2跳 friend 的帖** | **0 个 friend / 0 帖** | **0 个 friend / 0 帖** |

**neo4j 也判定该交集为 0** —— 所以推导路径给出的「93 个 friend / 381 帖」是真实缺陷。

## 最终定位：第二跳终点被解析成 `friend` 而非 `person`

在受控 fixture 上按比较序号观测等式 Filter（fixture 内部 id：
person1=360449、friend2=360451、friend3=360452、t1=360450）：

```
[EQ#5] i64(360450)  vs i64(360449)  -> false    <- 到达 tag，非终点
[EQ#6] i64(360450)  vs i64(360449)  -> false
[EQ#7] VVal(360451) vs VVal(360449) -> false    <- 终点 = friend2，而非 person1
[EQ#8] VVal(360452) vs VVal(360449) -> false    <- 终点 = friend3
```

判别实验：

| 第二跳终点写法 | 结果 |
|---|---|
| `(person)`（关联变量） | 0 ❌ |
| `(friend)`（另一个已绑定变量） | **0** ❌ |
| `(:FPerson {pid:1})`（常量） | **1** ✅ |

**两个不同的绑定变量都失效，常量有效** —— 说明问题不在「哪个变量」，而在
**「把绑定变量作为 Expand 终点」这条路径本身**。

计划结构本身是正确的：

```
Expand(src=x, dst=__anon_3, labels=[23]=FTag, OUT)
Expand(src=__anon_3, dst=__exists_dst_1, labels=[20]=FINT, IN)
CorrelatedSource |slots: 9 11 |      <- col0=person, col1=posts
```

且 `dst_bound_` 为假（`[EXP]` 插桩无输出），即第二跳走**自由展开**，
随后由 `Filter(__exists_dst_1 == __exists_saved_1)` 约束 ——
但观测显示 `__exists_saved_1` 一侧拿到了**顶点而比较不通过**，
即该列的值与 `person` 不一致（`EQ#7/8` 的左值恰为两个 friend 的 id）。

**下次起点**：核对 `bindExistsSubPlan` 中 `sub_dst_var` / `saved_var` 的 slot 与
`ProjectionExtract` 实际透传的列是否对齐（`slots: 9 4 12 … 13 14 … 3` 中的
slot 4 与 slot 3 分别对应 saved 与 dst），确认 `__exists_saved_1` 是否真正承载
被关联变量的值。

## 本轮补充实验与更正

**更正一个无效实验**：我用「单跳 `(z:post)-[:FINT]-(pp)`」去验证变量终点，结果两引擎都为 0 ——
但那是**我的用例设计错误**：fixture 里 `FINT` 是 `person -> tag`，post 上根本没有 FINT 边，
所以 0 是正确结果，**不构成对「变量终点失效」的支持或反驳**。记录以免后续误引。

**仍然成立的核心判别**（受控 fixture，`/tmp/probe/c10.py`）：

| 第二跳终点 | 结果 | |
|---|---|---|
| `(person)` 关联变量 | 0 | 错 |
| **`(friend)` 另一个绑定变量** | **0** | 错 |
| `(:FPerson {pid:1})` 常量 | 1 | 对 |

两个不同绑定变量都失效、常量有效 ⇒ **问题在「把绑定变量用作 Expand 终点」这条路径**，
与变量名无关。

**等式 Filter 观测**（fixture 内部 id：person1=360449、friend2=360451、friend3=360452、t1=360450）：

```
[EQ#7] VVal(360451) vs VVal(360449) -> false
[EQ#8] VVal(360452) vs VVal(360449) -> false
```

终点列的值是 **friend** 的 id，而另一侧是 person 的 id。
`dst_bound_` 为假（`[EXP]` 插桩无输出），故第二跳自由展开后由该 Filter 约束。

**尚未完成**：`sub_dst_var` / `saved_var` 的 slot 与 `ProjectionExtract` 透传列的对齐核对。
计划中内层 `ProjectionExtract(specs=[x, __exists_saved_1, __anon_edge_2, …, __exists_dst_1])`
的 `slots` 为 `9 4 12 2147483649 13 14 2147483651 3`，即 `__exists_saved_1`→slot 4、
`__exists_dst_1`→slot 3。下一步应验证 slot 4 在运行期承载的是否真为 person（360449）。

## 根因定位完成：内层子计划的 schema 缺少被消费的关联变量，导致列错位

逐算子插桩（`EUGRAPH_DBG_PE` + `EUGRAPH_DBG_UNW` + `EUGRAPH_DBG_FILT`）把链路完整串起来了。

**1) Unwind 读到的列表是正确的**

```
[UNW] out_col=3 n_in=3 list=[ VVal(360455) VVal(360456)]   <- post 元素，正确
[UNW] out_col=3 n_in=3 list=[ VVal(360457)]
```

**2) 但 Filter 看到的 `x` 是 friend**

```
[FILT] c0=VRef(360451)=friend  c1=VVal(360451)=friend  c2=VRef(360449)=person  c3=idx6(edge)  c4=VRef(360450)=tag
```

`c2`（应为 `posts` 或 `x`）是 person，**`posts` 整个消失了**。

**3) 决定性证据：某个 ProjectionExtract 的输入 schema 里没有 `posts`**

```
[PE] this=0x…e020 n_specs=3 n_in_cols=2 in_schema=[ friend __exists_saved_1]:
     out0<-src0=VRef(360451) out1<-src0=VRef(360451) out2<-src1=VRef(360449)
```

这个 PE 的输入 schema 是 `[friend, __exists_saved_1]` —— **`posts` 不在其中**。
于是 `__exists_saved_1` 占的是**列 1**，而关联注入的 `[person, posts]` 把**列 1 填成了 `posts`**，
`__exists_saved_1` 实际拿到的是**帖子列表**，与终点做等式比较必然全不通过 → `s = 0`。

**4) 关联本身构建正确**

```
[CORR2] var=person right_col=0 left_col=1 left_slot=1
[CORR2] var=posts  right_col=1 left_col=2 left_slot=8
```

`CorrelatedSource` 按 `variables` 顺序产出 `[person, posts]`，但下游 schema 只声明了
`[friend, __exists_saved_1]` —— **产出与消费的列布局不一致**。

### 性质与修法方向

这是一个**列布局契约不一致**问题，不是关联值取错：

* 产出侧（`CorrelatedSource`）：按 `source.variables` / `column_indices` 逐列产出；
* 消费侧（内层 `ExistsExpr` 子计划的 `ProjectionExtract` / `Filter`）：按自己的
  `input_schema_` 定位列，而该 schema 里**不含**被 `Unwind` 消费掉的关联变量 `posts`。

即：`ctx_.beginSubScope()` 之后注册进 `ctx_.symbols` 的关联变量集合，与
下游 PE 实际透传的列表**不一致**（`posts` 被用于 `Unwind` 的 `list_expr`，但未出现在
PE 的输入 schema 中，或其位置被后续追加列挤掉）。

**修法方向**：让内层子计划的输入 schema 与 `CorrelatedSource` 的产出列**严格一致**
（同名同序），或在解析关联列一律走 `slot_layout.getColumnIndex(slot)` 而非按名/schema 位置。
下一轮应先确认 `in_schema=[ friend __exists_saved_1]` 这个 PE 属于哪一层、其 schema 由谁构造。

## 尝试的修复（未生效，已撤回）

按「列布局契约不一致」的思路实现了一版修复：给 `CorrelatedSourcePhysicalOp` 增加**按 slot 注入**的
接口（`setSlottedValues` + `slots_`），规划器把 `val.slot_ids` 传进算子，Apply 侧把
`(left_slot, value)` 成对交给 source，由 source 按自己的 `slots_` 顺序落位，
以消除「产出顺序 vs 消费位置」的错位。

**结果：C2/C4 仍然错误，且打破了一个既有测试**
（`QueryExecutorTest.NotBarePatternExpressionInReturn`），因此**整版撤回**，未提交。
撤回后 `query_executor_tests` 恢复 521/521。

**失败的含义**：错位**不是**发生在「Apply → CorrelatedSource 的值搬运」这一层，
而在更深处的 **schema 构造** —— 那个 `in_schema=[ friend __exists_saved_1]` 的
`ProjectionExtract` 属于内层 EXISTS 子计划，它的输入 schema **本就不含 `posts`**，
所以无论注入顺序如何调整，`posts` 都到不了需要它的位置。

**下一轮应从 schema 构造入手**（而非值搬运）：

* 内层 EXISTS 子计划（`bindExistsSubPlan`）构造 `ProjectionExtract` / `CorrelatedSource`
  的输出 schema 时，`posts` 为何缺席 —— 它是 `Unwind` 的 `list_expr` 所需的关联变量；
* 候选方向：让内层子计划的输入 schema 覆盖**所有被其下游算子引用的关联变量**
  （含被 `Unwind` 消费的列表），或把 `Unwind` 的 `list_expr` 改为从 source 直接取用
  而不再依赖一次 `ProjectionExtract` 透传。

### 本轮产出的复现器（重建）

`/tmp/probe/c10.py` —— 自建 `:FPerson/:FTag/:FPost` 受控数据，在两引擎上跑
C1（两跳·不引用）/ C2（两跳·引用变量）/ C3（两跳·常量）/ C4（complex-10 形状）并逐条对照。
当前结果：**C1、C3 一致；C2、C4 不一致**。

## 真正的根因（已确认到代码行与失效机制）

**位置**：`bindExistsSubPlan`，`src/query/planner/binder/bind_match.cpp` 约 1306-1320 行。

```cpp
BoundCorrelatedSourceOp source;
for (const auto& [outer_slot, sub_slot] : correlation) {
    for (const auto& [name, info] : ctx_.symbols) {   // ← 遍历 unordered_map 反查名字
        if (sub_slot == info.slot_id) {
            source.variables.push_back(name);          // ← 取到「第一个碰巧同 slot 的名字」
            source.types.push_back(std::move(topo));
            source.column_indices.push_back(info.column_index);
            source.slot_ids.push_back(sub_slot);
            break;
        }
    }
}
```

**`correlation` 只保存 slot 对，不保存变量名**，于是构建 `source.variables` 时只能
**按 slot 在 `ctx_.symbols` 里反查名字**。而：

* `ctx_.symbols` 是 `std::unordered_map<std::string, ColumnInfo>` —— 遍历顺序由哈希决定，**不确定**；
* **同一个 slot 上可能坐着多个名字**：`bindExistsSubPlan` 为被关联的外层变量另建了
  `__exists_saved_N`，它**与原变量共用同一个 slot**。

**实测证据**（受控 fixture，`EUGRAPH_DBG_SRCORDER`）：

```
correlation pairs: (outer9,sub9) (outer1,sub11)
looking for slot=9  -> picked name=x
looking for slot=11 -> picked name=__exists_saved_1        <- 本该是 posts！
ctx has: __exists_saved_1(slot11,col2) __exists_dst_1(slot10,col1) x(slot9,col0)
```

**失效链条**：

1. `source.variables` 变成 `[x, __exists_saved_1]`，而不是 `[x, posts]`；
2. `source.types` 随之**类型取错**（`posts` 是 LIST，被取成 VERTEX）；
3. `source.column_indices` 也错 → 下游 `ProjectionExtract` / `Unwind` 按错误布局取值；
4. `Unwind` 的 `list_expr` 取到的不是帖子列表 → 收集为空 → **`size` 得 0**。

### 这一根因解释了此前全部「想不通」的观察

| 此前的困惑 | 现在的解释 |
|---|---|
| 常量终点对、变量终点错 | 常量不引入关联对，不触发这次反查 |
| `(person)` 与 `(friend)` 都错 | 两者都走反查；命中哪个名字只取决于 unordered_map 顺序，与写的是哪个变量无关 |
| 注入的值明明正确（`[person, posts]`） | 注入无误；**错的是 source 声明的变量名/类型/列号**，两侧契约就此断裂 |
| 加「按 slot 注入」没修好 | 错的是**声明的身份**，不是搬运顺序 —— 这也解释了那次修复为何无效 |
| sf0.1 上「93 friend / 381 帖」 | 同一机制：解析出的变量与预期不符，约束大面积失效 |

### 与 AGENTS.md 红线的关系

AGENTS.md 规定 **`VariableId = SlotId`**，禁止以变量名作为语义身份。这里是**反向**的同类错误：
手上已有 slot，却回头**遍历 map 按名字反查**，而 map 中名字与 slot 并非一一对应。

**修复方向**：让关联对**显式携带变量名**（`correlation` 增加 `var_name` 字段），
构建 `source.variables` 时直接取用，**彻底不做反查**。

## 修复尝试二（未生效，已撤回）

按上面的根因实现：给 `Correlation` 增加 `right_var`，四个构造点显式携带子计划侧的变量名，
`bindExistsSubPlan` 构建 `source.variables` 时**直接取用、不再反查** `ctx_.symbols`；
同时把 `BoundSemiJoinOp::correlation` 统一成同一 `Correlation` 类型
（连带更新 `operator_hash` / `operator_eq` / 物理规划器的结构化绑定）。

**结果：521/521 全绿，但 C2/C4 仍然错误** —— 故障行为**完全没变**。
因此按「不提交未经证实的改动」原则**整版撤回**。

**这次失败排除了什么**：反查**确实存在且确实有歧义**（实测 `slot=11 -> __exists_saved_1`
而本该是 `posts`），但它**不是充分原因** —— 去掉反查后结果不变，说明
**`source.variables` 即便取到正确名字，下游仍然拿不到 `posts`**。

**指向真正的问题**：内层 EXISTS 子计划的 `Correlation` 列表里，
`posts` 这一项对应的 `right_slot`（实测 11）在 `ctx_.symbols` 中**本来就被
`__exists_saved_1` 占着**，即 `posts` 在子计划作用域里**从未以该 slot 注册过**。
所以问题在**更上游**：`posts` 作为 `Unwind` 的 `list_expr` 所需的关联变量，
在 `bindExistsSubPlan` 构造子计划时**没有被正确注册进子作用域**（或注册到了别的 slot）。
反查只是把这个「本就缺失」暴露成了「取错名字」的表象。

**下一轮应从「`posts` 何时、以哪个 slot 注册进子作用域」入手**，
而不是继续在名字/值搬运层调整。

## 精确证据：需要关联的那个变量从未注册进子作用域

在 `bindExistsSubPlan` 构建 source 之前打印**完整的 `ctx_.symbols`**（`EUGRAPH_DBG_SCOPE`），
受控 fixture 上一次查询内的两个子作用域：

```
[SCOPE] start=friend correlation_n=2 ctx_.symbols:
    __exists_saved_1 slot=4  col=2 kind=4
    __exists_dst_1   slot=3  col=1 kind=4
    friend           slot=2  col=0 kind=4

[SCOPE] start=x correlation_n=2 ctx_.symbols:
    __exists_saved_1 slot=11 col=2 kind=4
    __exists_dst_1   slot=10 col=1 kind=4
    x                slot=9  col=0 kind=15
```

**第二个子作用域里没有 `posts`。**

而它的 correlation 是 `(outer9,sub9) (outer1,sub11)`：

* `sub9` → `x`（slot 9）—— 与 correlation 的意图一致；
* **`sub11` → `__exists_saved_1`，但 correlation 的意图是 `posts`**；
* **`posts` 在整个子作用域里根本不存在**，所以 slot 11 上坐的是别的变量。

### 结论（可证伪的精确陈述）

**`posts`（即 `Unwind` 的 `list_expr` 所需的那个列表）在
`bindExistsSubPlan` 构造这个内层子计划时，从未被注册进子作用域。**

这解释了此前两次修复为何都无效：

| 尝试 | 为何无效 |
|---|---|
| 按 slot 注入值（改搬运顺序） | 名字/类型/列号本身就不对，顺序无关 |
| 消除按 slot 反查名字（改取名方式） | 名字取对了也没用 —— **那个绑定压根不存在** |

反查歧义只是把一个**本就缺失的绑定**表现为「取到了错误的名字」。

### 下一轮的唯一目标

查清 **`posts` 为何没有进入该子作用域**。已知线索：

* 该子计划由 `bindPatternComprehension`（`bind_match.cpp` ~1784-1839）**合成 `ExistsExpr`**
  后复用 `bindExistsSubPlan` 构建；
* 合成路径**不做** `bindExistsSubPlan` 里那套「起始变量 / 链终点 / extra_corr_vars」的
  注册改写 —— 它只克隆 `pc.patterns` 与 `pc.where_pred`，**没有传递
  「外层推导的列表变量（`posts`）需要作为关联变量」这一信息**；
* 而 `outer_vars`（外层 `lowerListComprehensionWithPatternComprehension` 收集）
  确实包含 `posts`，关联对也是在那里生成的 —— 断点在**从
  `lowerListComprehensionWithPatternComprehension` 到 `bindPatternComprehension`
  再到 `bindExistsSubPlan` 的这段传递**上。

**验收**：`bindExistsSubPlan` 的 `ctx_.symbols` 中出现 `posts`（类型 LIST），
且 correlation 中 `posts` 对应的 `sub_slot` 指向它。

## 实现路径（下一步具体动作）

断点已确定在**合成路径不做关联注册**：

`bindPatternComprehension`（`bind_match.cpp` ~1783）合成 `ExistsExpr` 时只克隆
`pc.patterns` 与 `pc.where_pred`，随后调用 `bindExistsSubPlan(synthetic, exists_corr)`。
而 `bindExistsSubPlan` 内部只为三类变量建立子作用域绑定：

1. 起始变量（`start_var_name`）；
2. 链终点（`saved_chain_corrs` → `__exists_saved_N` / `__exists_dst_N`）；
3. `extra_corr_vars`（从 `where_pred` 里收集的**上层作用域**变量）。

**`posts` 不属于这三类** —— 它是**外层列表推导的循环输入**（`Unwind` 的 `list_expr`），
既不是模式的起始/终点变量，也不出现在模式的 `where_pred` 里，所以**没有任何一步把它注册进子作用域**。

### 具体动作

给 `bindPatternComprehension` 增加一条显式入口，把「该模式推导需要、且在外层已绑定的变量」
（此处即 `posts`）作为**预置关联变量**传给 `bindExistsSubPlan`，并在 `beginSubScope()` 之后
立即注册进 `ctx_.symbols`（分配 `column_index`、沿用其 `slot_id`、类型取 LIST），
同时把 `(outer_slot, sub_slot, name)` 推进 `correlation`。

**代码位置**：
* `bindPatternComprehension`（~1839）调用处：列出需关联的外层变量并传入；
* `bindExistsSubPlan`（~991 `extra_corr_vars` 附近）：新增参数 `preset_corr_vars`，
  在 `beginSubScope()` 后注册，复用已有的 `extra_corr_vars` 注册逻辑（~1290-1304）。

**验收**：
1. `EUGRAPH_DBG_SCOPE` 打印的第二个子作用域中出现 `posts slot=<n> kind=15(LIST)`；
2. `correlation` 中 `posts` 的 `sub_slot` 指向它；
3. `/tmp/probe/c10.py` 的 **C2 与 C4 与 neo4j 一致**（`s=1`、`cpc=1`）；
4. `query_executor_tests` 521/521、`optimizer_tests` 109/109；
5. 回归到 sf0.1：complex-10 的 10 行分数与独立路径 `count(DISTINCT q)` 一致
   （当前该独立路径给出 0 个共同兴趣 friend，而推导给出 93/381 —— 修复后应一致）。

## 第四次尝试：找到并打通了前两环，第三环暴露后撤回

本轮把断点链条完整走通了三环，其中**两环确认是真实缺陷**，第三环暴露了新的破坏，故整版撤回。

### 第一环（真实缺陷）：`posts` 从不进入子作用域——原因确认

`bindExistsSubPlan` 里 `extra_corr_vars` 的收集**只在 `is_full_query` 分支内执行**（约 995 行
`if (is_full_query) {`）。模式推导走的是 `synthetic` 路径（`exists.full_query == nullptr`），
所以那段收集**根本不运行**。

实测（`EUGRAPH_DBG_ECV`）：

```
[ECV] is_full_query=0 extra_corr_vars=[ friend person]
[ECV] is_full_query=0 extra_corr_vars=[ person x]
```

**`posts` 不在其中** —— 因为模式是 `(x)-[:FTAG]->()<-[:FINT]-(person)`，
**`posts` 压根不是模式的变量**，它是**外层 `Unwind` 的 list 输入**（`bind_return.cpp:1176`
`unwind->list_expr`），从不出现在模式推导的 AST 里。所以无论怎么遍历模式的谓词都找不到它。

**结论**：必须由 `bindPatternComprehension` **显式**把「child 计划（外层 Unwind）所依赖的
外层变量」传给 `bindExistsSubPlan`。实测这条路径可行：传入后
`[PR] preset=[ posts] subscope=[ posts(slot8,kind4) ... ]` —— **`posts` 进入了子作用域**。

### 第二环（真实缺陷）：注册时类型被硬编码成 VERTEX

`bindExistsSubPlan` 约 1314 行注册 `extra_corr_vars` 时：

```cpp
ci.type = BoundType::Vertex();     // 硬编码
```

对 `posts`（**LIST**）来说类型就是错的。改为从 `saved_ctx` 取真实类型后：

```
[PR] preset=[ posts] subscope=[ posts(slot8,kind10) ... ]     <- kind10 = LIST，正确
```

### 第三环（新暴露的问题，导致仍需撤回）

修好前两环后 `source.variables` 变成：

```
[SRC2] vars=[ x __exists_saved_1 posts] types=[ 15 7 10] cols=[ 0 2 3] slots=[ 9 11 8 ]
```

**`__exists_saved_1` 被插在 `x` 与 `posts` 之间** —— 即 `correlation` 里多了一条
指向 `__exists_saved_1` 的项（它是内层链终点保存用的，本不该成为 source 的可见列）。
它的插入使 `posts` 的列位置后移，下游按预期位置取值仍取不到 → `s` 仍为 0。

**且结果与修复前完全一致**（C2/C4 都是 0），说明这一层的破坏抵消了前两环的收益。

### 本轮判定的取舍

前两环是**独立成立的真实缺陷**，但第三环（source 变量集合里混入
`__exists_saved_1`）不解决的话，单独提交前两环**不会改变任何行为**，
只是把「缺失」换成「位置错」——这属于未验证的改动，按原则撤回。

**下一轮的完整动作**（三环一起修才有意义）：

1. `bindPatternComprehension` → 把 child（`BoundUnwindOp`）的 `list_expr` 若是
   `BoundColumnRef` 且在外层可解析，则作为 `preset_corr_vars` 传入 `bindExistsSubPlan`；
2. `bindExistsSubPlan` 注册这些 preset 时**类型取自 `saved_ctx`**，不要硬编码 `Vertex()`；
3. **`__exists_saved_N` / `__exists_dst_N` 不应进入 `source.variables`** —— 它们是
   子计划内部的保存槽，不是外部注入的关联列。需在建 `source` 时按
   「只收真正来自外层的关联变量」过滤（或让 `correlation` 区分
   「注入列」与「内部保存槽」两类用途）。

## 第五次尝试：五处修好四处，裁剪关联引入回归，整体撤回

本轮把三环全部实现，并在过程中发现还需两处（共五处）。五处的实测状态：

| # | 改动 | 实测结果 |
|---|---|---|
| 1 | `bindPatternComprehension` 把 child（`BoundUnwindOp`）的 `list_expr` 列名作为 `preset_corr_vars` 传入 `bindExistsSubPlan` | ✅ **生效**：`[PR] preset=[ posts]` |
| 2 | 注册 preset 时类型取自 `saved_ctx`，不硬编码 `Vertex()` | ✅ **生效**：`posts(slot8,kind10)` = LIST |
| 3 | `source.variables` 过滤掉 `__exists_*` 内部保存槽 | ✅ **生效**：`vars=[x posts]`（不再是 `[x __exists_saved_1 posts]`） |
| 4 | 计划结构随之变正确 | ✅ `CorrelatedSource |slots: 9 8 |`、`ProjectionExtract(specs=[x, posts, …]) |slots: 9 8 … |` |
| 5 | 裁剪 `correlation` 使其与 `source.slot_ids` 严格一致 | ⚠️ 裁掉了多余项（`n_vals` 由 3 → 2），**但结果仍为 0**，且**打破 3 个既有测试** |

**测试回归**：`QueryExecutorTest.NestedExistsWithCorrelatedPropertyFilter` 等 3 个失败
→ 说明第 5 处（按 slot 裁剪 correlation）**破坏了嵌套 EXISTS 的既有语义**，
因为 `__exists_saved_N` 那条关联对**并非多余** —— 它承担 SemiJoin 注入保存值的职责，
不能简单从 `correlation` 中删除。

**因此整体撤回**，`521/521` 恢复。

### 关键认识（修正上一轮的判断）

上一轮我把 `__exists_saved_1` 混入 `source.variables` 判为「应过滤掉」。
第 3 处按此实现后，**source 变量集合确实正确了**，但紧接着暴露：
**`correlation` 与 `source.variables` 是两种不同的东西** ——

* `source.variables` = **要暴露成列的注入变量**（`x`、`posts`）；
* `correlation` = **Apply/SemiJoin 取值的配对表**，其中 `__exists_saved_N`
  是 SemiJoin 需要注入的**保存值**，必须保留。

第 5 处用同一份 `correlation` 去驱动 source 的列，导致两者语义被混同 → 测试回归。

### 下一轮的正确方向

**不应裁剪 `correlation`**，而应让 `PatternComprehensionApplyPhysicalOp` /
`SemiJoinPhysicalOp` **按 `source.slot_ids` 决定注入哪些值**（即注入集合由 source 的
可见变量定义），`correlation` 仅作为「slot → 外层列」的查找表。这样：

* `source.variables` = `[x, posts]`（列）；
* 注入值 = 按 `source.slot_ids` 从 `correlation` 查出对应的外层列（2 个）；
* `__exists_saved_N` 仍留在 `correlation` 中供 SemiJoin 使用，但不出现在 source 的列里。

即需要修改的是**物理算子的取值侧**（按 source 的 slot 列表注入），
而不是绑定器的 correlation 列表。

## 第六次尝试：两处修好，第三处与既有语义冲突，撤回（附精确隔离结论）

本轮按上一轮修正后的方向实现，并**逐处隔离**了回归来源。结果如下。

### 已验证有效的改动（撤回前实测）

| 改动 | 实测证据 |
|---|---|
| `correlation` 显式携带子计划侧变量名（`right_var`） | 编译通过、521/521 |
| `source.variables` 过滤 `__exists_*` 内部槽 | `vars=[x posts]`（此前 `[x __exists_saved_1 posts]`） |
| 注册类型取自 `saved_ctx`（不硬编码 Vertex） | `posts` 类型为 LIST |
| Apply 按 `source.publishedSlots()` 决定注入集合 | 注入值与列数对齐（`n_vals` 3 → 2） |

### 精确的隔离实验（本轮最有价值的产出）

用「逐处禁用 + 跑 521」的方式定位，结论是**确定的**：

| 配置 | 结果 |
|---|---|
| baseline（全部撤回） | **521/521 通过** |
| 仅启用 Apply 注入改动（`published_cols = left_corr_cols` 之外的全关） | **521/521 通过** |
| 启用 `source.variables` 过滤 `__exists_*` | **3 个失败** |
| 过滤 + 保留原注入 | 3 个失败 |
| 过滤关闭 + Apply 注入改动开启 | **1 个失败** |
| 全部关闭 | 521/521 通过 |

失败集合固定为：

```
QueryExecutorTest.PatternPredicateTwoNodes
QueryExecutorTest.NotBarePatternExpressionInReturn
QueryExecutorTest.NestedExistsWithCorrelatedPropertyFilter
```

### 结论：`__exists_saved_N` 不能从 `source.variables` 中移除

隔离实验表明：**`__exists_saved_N` 必须留在 `source.variables` 里**，
因为**SemiJoin 路径依赖它作为「可注入列」**（嵌套 EXISTS 的保存值注入走同一条 source）。
把它过滤掉会让 `PatternPredicateTwoNodes` / `NestedExistsWithCorrelatedPropertyFilter` 失败。

**这推翻了我上一轮的方向**：问题**不在**「source 多暴露了一列」，而在
**Apply 与 SemiJoin 对同一个 source 的列集合有不同的消费需求**：

* **SemiJoin**：需要 `__exists_saved_N` 作为注入列（保存值）；
* **PatternComprehensionApply**：不需要它，只需要循环变量与列表。

两者共用 `BoundCorrelatedSourceOp`，所以**任何单一列集合都无法同时满足**。

### 因此正确的修法（下一轮方向，本轮未实现）

**不是改列集合，而是让两个消费者各自决定注入集合**：

* `source.variables` **保持现状**（含 `__exists_saved_N`），SemiJoin 不受影响；
* `PatternComprehensionApplyPhysicalOp` 的注入集合**只取右子计划实际引用的 slot**
  —— 即用 `right_slot_layout`（右子计划的 TupleSlotLayout）过滤，
  **而不是用 `source.publishedSlots()`**。

关键区别：`publishedSlots()` 是 source 的**全部**列（含保存槽），
而「右子计划实际引用的 slot 集合」才是 Apply 该注入的集合。
上一轮我用的正是 `publishedSlots()`，所以它等价于「不过滤」，也就无法修好；
而我对它的有效性判断（`n_vals 3 → 2`）来自 link 3 同时生效，
即**我把两个改动叠加后的效果误判成了单个改动的效果**。

**这是本轮最该记下的教训**：`n_vals` 的变化来自 link 3，不是来自 Apply 注入改动；
我在未分离两个改动的情况下判断了后者的有效性。

## 判决性实验：结论是否定的，该方向被排除

按上一轮预先约定的判据执行了实验：

**改动**：只在**模式推导路径**（`preset_corr_vars` 非空，即唯一的推导调用方）把
`__exists_saved_N` 从 `source.variables` 中过滤掉，**保留 EXISTS/SemiJoin 路径不变**。

**双向判据结果**：

| 判据 | 结果 |
|---|---|
| 521/521 是否保持 | ✅ **保持**（条件过滤确实没有影响 EXISTS 路径，验证了「两路径需求冲突」的判断） |
| C2/C4 是否变对 | ❌ **未变**（C2 仍 `s=0`，neo4j 为 1） |

**结论：`Apply 与 SemiJoin 共用 source、需求冲突` 这一机制是真实的，但它不是 C2/C4 的成因。**

这是**预先约定的排除条件**，因此该方向到此终止，不再继续叠加改动。

### 至此被排除的方向（按时间顺序，均已实测反证）

| # | 方向 | 反证 |
|---|---|---|
| 1 | 缺少按元素去重 | 导出的元素互不相同，去重是 no-op |
| 2 | 循环变量名冲突 | 异名同样失败 |
| 3 | SlotId 未传递/未关联 | 计划已正确关联 |
| 4 | 值搬运顺序（按 slot 注入） | 去掉后行为不变 |
| 5 | 按 slot 反查变量名有歧义 | 消除后行为不变（歧义真实但非充分） |
| 6 | 关联变量未注册（`posts` 缺失） | 补上后行为不变 |
| 7 | 类型硬编码 `Vertex()` | 修正后行为不变 |
| 8 | Apply/SemiJoin 列集合冲突 | **本轮判决实验排除** |

**第 4-8 项都是「真实缺陷」但都不是该 bug 的成因** —— 这一点值得记录：
该函数里存在多个独立缺陷，逐个修好都不会改变 C2/C4 的行为。

### 回到唯一未被否证的原始观测

经过八轮排除，仍然成立且未被解释的观测只有一个：

```
[FILT] c0=VRef(360451)=friend  c2=VRef(360449)=person  c4=VRef(360450)=tag
```

即 **`Unwind` 之后、进入第二跳 `Expand` 的 `x` 是 friend，而不是 post**；
而 Unwind 自己读到的列表是正确的 post 列表（`[VVal(360455) VVal(360456)]`）。

**唯一未验证的环节是：`x` 从 Unwind 的输出列（列 3）到 Filter 的输入列（列 0）
之间，经过了 `ProjectionExtract`；而 `ProjectionExtract` 内部构造过
`ProjectionExtract(specs=[…, __exists_dst_1<pass>])` 这类带 `ctor-vertex` 的 spec。**
下一步应在**该 ProjectionExtract 的 spec 解析**处观测（而非继续在 source/关联层面），
因为这是「正确的列表元素 → 错误的元素」之间唯一的中间环节。

## 触发条件终于确定：`NOT <反模式谓词>`（十轮后）

按「从已知可复现的一侧做二分」的方法，用受控 fixture 逐步删减元素，**定位到了唯一触发条件**。

**最小复现器**（7 条 fixture，单元测试
`DISABLED_NegatedPatternPredicateDoesNotBreakFollowingComprehension`）：

```
MATCH (person:UT_P {id: 1})-[:UT_KNOWS]->(m:UT_P {id: 99})-[:UT_KNOWS]->(friend:UT_F)
WHERE NOT friend=person
      AND NOT (friend)-[:UT_KNOWS]-(person)        <- 触发条件
OPTIONAL MATCH (friend)<-[:UT_CREATED]-(post:UT_Post)
WITH friend, collect(post) AS posts, person
RETURN size(posts) AS n,
       size([x IN posts WHERE (x)-[:UT_HAS_TAG]->()<-[:UT_INTEREST]-(person)]) AS cpc
```

| 变体 | eugraph | neo4j |
|---|---|---|
| 无 `NOT (…)` | `cpc = 1` ✅ | 1 |
| **有 `NOT (…)`** | **`cpc = 0`** ❌ | 1 |
| `NOT (friend)<-[:UT_KNOWS]-(person)`（正向） | `cpc = 0` ❌ | 1 |

**逐个加回元素的二分过程**（受控 fixture）：

| 变体 | eugraph | 结论 |
|---|---|---|
| W1 基线（无 NOT / 无 datetime / 无 pc） | 1 ✅ | |
| **W2 加 `NOT (friend)-[:UT_KNOWS]-(person)`** | **0** ❌ | **触发** |
| W3 加 `datetime({epochMillis: …})` 过滤 | 1 ✅ | 无关 |
| W4 加 `size(posts) AS pc` 投影 | 1 ✅ | 无关 |
| V2/V3 两跳（固定 / VLE） | 1 ✅ | 无关 |
| V1 单跳到达 friend | 空集 | 该约束下无结果，故必须两跳 |

**因此：`datetime`、变长路径、`pc`/`cpc` 共存、slot/列布局、关联注册 —— 全部与本缺陷无关。**
前面九轮追查的方向都是**真实但无关**的缺陷；真正压制推导终点约束的是
**同一个 WHERE 里存在一个 `NOT <反模式谓词>`**。

**这也解释了为什么单元测试此前一直通过**：我构造的 fixture 里没有 `NOT (…)`。

### 与 complex-10 的关系

complex-10 的 WHERE 正是
`WHERE NOT friend=person AND NOT (friend)-[:KNOWS]-(person)` —— **逐字命中该触发条件**，
所以它在 sf0.1 上给出错误的 `commonPostCount`（93 friend / 381 帖 vs 独立路径的 0）。

### 下一步

触发条件已知且极简，方向明确：`bindExistsSubPlan` 处理 `NOT <反模式谓词>`（反析取 /
anti-join 路径）时，对**同一 WHERE 中后续的模式推导**产生了副作用 ——
应查该路径对 `ctx_.symbols` / `correlation` 的写入是否泄漏到了兄弟谓词，
而不是继续在 source/列层面追查。

## 结案：complex-10 已完全正确（并更正两处我自己的误判）

### 修复

根因是 **`bindExistsSubPlan` 用局部计数器命名保存槽**
（`__exists_saved_<chain_counter>`，每次调用从 0 开始）。同一条语句里编译的
**两个子计划都会生成 `__exists_saved_1`**，二者都注册进 `ctx_.symbols`，
于是后编译者解析到前者的绑定 —— 推导读到**别的子计划的保存值**，静默丢弃终点约束。

修法：后缀改用 binder 全局计数器 `nextAnonId()`（与 `__lc_N` / `__pc_N` 同源）。
复现器 `NegatedPatternPredicateDoesNotBreakFollowingComprehension` 由 `DISABLED_` 转为启用。

### 验证（修复后）

**complex-10 逐行核验**（用**不含任何推导**的基础 MATCH 重算 `pc` / `cpc`，再比 `score = 2*cpc - pc`）：

```
3 个 personId × 2 个月份 = 60 行，不一致 0 处
且分数非零（如 933/month5：+1,0,0,-1,-1,-1,-3,-5,-7,-13）
```

**comprehension 与 neo4j 直接对照**（同一 friend `24189255811663`）：

```
eugraph: {n: 95, cpc: 6}
neo4j  : {n: 95, cpc: 6}     一致
```

`query_executor_tests` **524/524**、`optimizer_tests` **109/109**、格式通过。

### 更正一：我此前报告的「sf0.1 上 93 friend / 381 帖 vs 独立路径 0」是**误判**

那个对比**用了两套不等价的约束**（一侧含 `NOT (friend)-[:KNOWS]-(person)`、另一侧不含），
所以「0 vs 381」是**测量错误**，不是缺陷证据。正确做法是**逐行用基础 MATCH 重算同一量**，
而这样核验的结果是**完全一致**。

### 更正二：我一度以为「推导过度上报」，实际是**少数**

同一 friend 上：

| 查询 | 结果 |
|---|---|
| 推导 `size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(person)])` | 6 |
| 基础 MATCH `count(DISTINCT post)` | 30 |

我最初把 6 与 30 的差异当成缺陷；实际两者**语义不同**（前者受约束于该 friend 的帖集合与
另一个 person 的兴趣，后者是另一条路径），且**推导与 neo4j 都是 6**。

### 方法上的教训（本会话最贵的一课）

**十一轮追查中，前九轮找到的都是「真实但无关」的缺陷**；真正的根因是一个
**命名冲突**（同一个生成名被两条支路复用）。而它最终是靠**用户建议的两件事**找到的：

1. **对照 neo4j** —— 迫使我怀疑自己的期望值，而不是继续猜机制；
2. **写最小单元测试** —— 把触发条件固化成 7 行 fixture，于是命名冲突一目了然。

此后又靠**逐行基础 MATCH 核验**发现我自己的「仍然错误」结论也是误判。
**两次关键转折都来自「换一个独立视角去核对」，而不是更深入地推理。**
