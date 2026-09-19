# 变长展开（VLE）性能剖析：瓶颈定位

> 目的：在着手优化 `VarLenExpand` 前，先**用测量确定瓶颈在哪**。
> 结论与最初基于计划形状的推断相反 —— **VLE 不是瓶颈**。

## 结论摘要

sf0.1，personId 933，CPU `scaling MHz ≈ 77%`，每轮新建连接（bolt 断开会让读数失真）：

| 阶段 | min / 单次 | 相对上一阶段 |
|---|---:|---:|
| S1 纯 2 跳 VLE（`[:KNOWS*2..2]`） | **5.87 ms** | — |
| S1b 纯 2 跳（personId 1242） | 5.54 ms | — |
| S2 + `NOT (f)-[:KNOWS]-(p)` | 138.24 ms | **+132 ms**（AntiSemiJoin） |
| S3 + `MATCH (f)-[:IS_LOCATED_IN]->(c:City)` | 147.83 ms | +10 ms |
| S4 + `datetime({epochMillis: f.birthday})` 过滤 | 165.39 ms | +18 ms |
| S5 + `OPTIONAL MATCH (f)<-[:HAS_CREATOR]-(post:Post)` | 396.75 ms | +231 ms |
| **S6 + 模式推导 `size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)])`** | **~5.9 s**（多次复测 5.88 / 5.89 / 6.18 / 6.58 s） | **+~5.5 s** |

**「纯 2 跳 VLE」只占 complex-10 总耗时的约 0.2%**（5.87 ms vs ~2.4–6 s）。

## 推导的代价随「组数」放大

同样的推导谓词，换外层取 friend 的方式：

| 外层 | 组数 | 耗时 |
|---|---:|---:|
| `MATCH (p)-[:KNOWS]->(f)`（单跳） | 3 | 257 ms |
| `MATCH (p)-[:KNOWS*2..2]-(f)`（2 跳） | 96 | **6957 ms** |
| 2 跳 + **无推导**（`sum(size(posts))`） | 96 | 397 ms |

单组成本都约 **60–85 ms/组**（257/3 ≈ 86，6957/96 ≈ 72）——即**代价与组数近似线性**，
而 VLE 本身从 3 组扩到 96 组只多花不到 1 ms（5.87 ms 总计）。

## 推导子计划形状

```
Aggregate(keys=0, aggs=1)                 ← collect
  Filter
    PatternComprehensionApply
      Unwind |slots: 1 2147483652 8 9 |   ← 每组一次 Unwind
        CorrelatedSource |slots: |
      Aggregate(keys=0, aggs=1)
        Filter                            ← HAS_INTEREST 匹配
          Expand(src=__anon_5, dst=__exists_dst_3, labels=[15], direction=IN)   ← ()<-[:HAS_INTEREST]-(p)
            Expand(src=x, dst=__anon_5, labels=[14], direction=OUT)             ← (x)-[:HAS_TAG]->
              CorrelatedSource
```

关键点：**每个 (组, 帖子) 组合都要跑一次完整的 Apply + Unwind + 两跳 Expand 链**。
`HAS_INTEREST` 的平均入度只有 2.2（35475 边 / 16080 tag），所以不是扇入爆炸，
而是**逐行建立相关子计划的固定开销**占主导。

## 优化方向（按预期收益排序，均未实施）

1. **推导退化为半连接（semi-join）+ 集合查找**：`(x)-[:HAS_TAG]->(t)` 得到的 tag 只要判断
   「是否在 `p` 的兴趣集合里」，无需再对 `t` 做一次 `<-[:HAS_INTEREST]-(p)` 的 IN 展开。
   把 `p` 的兴趣集合物化一次（每个 p 一次，而不是每个 (组,帖子) 一次），后续为 O(1) 判定。
2. **把 `size([...])` 在只用于计数时降级为 `count`**：`size` 会构造整张列表，
   而调用方常常只关心个数（`sum(size(...))`、`s > 0`）。已有 `existence_only` 的先例可借鉴。
3. **每组一次而非每帖一次**：`Unwind` 目前逐帖展开再聚合，可考虑先在组内批量处理。
4. VLE 本身（`direction=ANY` 的无向 2 跳）**暂不必优化**：实测 5.87 ms，收益上限极小。

## 测量注意

* **bolt 客户端长连接会断开**：单连接连跑多次慢查询（秒级）后会 `Failed to read from
  defunct connection`；每次新建连接，否则会把「连接断开」误判为崩溃或超时。
* **CPU 频率**：同一查询在 `scaling MHz` 不同时可有 3 倍差异（曾出现 18.8 s 与 5.9 s 两种读数）。
  跨时间比较同一查询前先确认频率。


## 推导优化的方向（neo4j 计划作为参照）

### neo4j 的计划：推导不是独立算子

对同一查询（pid 933）：

```
ProduceResults
  EagerAggregation
    OrderedAggregation            ← 按 (f, p, posts) 分组
      OptionalExpand(All)         ← OPTIONAL MATCH 取帖
        Filter
          VarLengthExpand(All)    ← 2 跳 KNOWS
            Filter
              NodeByLabelScan(p)
```

**推导完全没有出现在计划里** —— 它被编译进内联求值，不产生独立的 Apply 算子，
因此没有「逐 (组,帖) 建立子计划」的固定开销。

### 我们的子计划：每个 (组, 帖) 一次完整管线

```
Aggregate(keys=0, aggs=1)                 ← collect
  Filter
    PatternComprehensionApply
      Unwind                              ← 逐帖展开
        CorrelatedSource
      Aggregate(keys=0, aggs=1)
        Filter                            ← HAS_INTEREST 匹配
          Expand(src=__anon_5, dst=__exists_dst_3, labels=[15], direction=IN)   ← ()<-[:HAS_INTEREST]-(p)
            Expand(src=x, dst=__anon_5, labels=[14], direction=OUT)             ← (x)-[:HAS_TAG]->
              CorrelatedSource
```

实测数据说明瓶颈在哪：

| 量 | 值 |
|---|---:|
| 组数（f 的个数） | **171** |
| 不同的 `p` 个数 | **1**（`id=933` 常量） |
| `p` 的兴趣 tag 数 | 70 |
| 推导内层待判定帖数 | **18,321** |
| 每组成本 | 60–85 ms |

即：**171 组、18,321 帖，全部在反复判定同一份 70 个兴趣**，
且每帖都要走一次「两跳 Expand + 相关子计划建立」。

### 候选改动（按收益/风险排序）

**A. 第二跳改为「集合成员判定」（推荐）**

`(x)-[:HAS_TAG]->(t)<-[:HAS_INTEREST]-(p)` 的语义是「`x` 存在一个 tag 落在 `p` 的兴趣集合里」。
把 `p` 的兴趣集合**物化一次**，后续对 `t` 做 O(1) 哈希判定，
取代目前对每个 tag 做一次 `<-[:HAS_INTEREST]-(p)` 的 IN 展开。

* 收益：消除 18,321 次第二跳展开；
* 与 neo4j 的差异同源（它同样不物化 Apply 子计划）；
* **注意**：`p` 在 171 组间是常量，但引擎目前按组重建子计划 —— 若要跨组复用，
  需要按 **slot + 值** 缓存（同一 slot 且值未变则复用），这与 AGENTS.md 的
  `VariableId = SlotId` 一致。

**B. `size(list)` 在只消费大小时降级为 `count`**

`size` 会构造整张列表；调用方常只关心个数（`sum(size(...))`、`size(...) > 0`）。
`existence_only` 已是同类先例（把 `size > 0` 降级为「找到第一条即停」），可扩展为
「只计数不建列表」。

**C. 展开方向反转：从 `p` 侧出发**

把第二跳写成从 `p` 出发沿 `HAS_INTEREST` 正向展开（`Expand(src=p, ...)`），
避免按 tag 做 `direction=IN` 的入边扫描。
实测 `HAS_INTEREST` 平均入度仅 2.2，但**高连接度 tag 可达 173**，
而当前是「每个帖子 tag 都扫一次入边」。

**D. 每组一次而非每帖一次**

`Unwind` 目前逐帖展开再聚合；可考虑组内批量处理，把「建立子计划」的次数从
`帖数` 降到 `组数`。


### 已确认的机制：第二跳是「未绑定终点的入边扫描」

第二跳在计划中是：

```
Expand(src=__anon_5, dst=__exists_dst_3, labels=[15], direction=IN)
  Filter(...)   ← 随后用 __exists_saved_3 与 __exists_dst_3 做等式过滤
```

`Expand` 支持「绑定终点」路径（`dst_bound_`：只保留 `neighbor_id == 绑定值` 的边，
`src/query/physical_plan/operator/expand_physical_op.cpp`），但此处 `dst` 是**新建列**
`__exists_dst_3` 而非输入列，因此 `dst_bound = false`：

```cpp
// physical_planner.cpp
int dst_existing = v.dst_variable.empty() ? -1 : findColumn(child_schema, v.dst_variable);
bool dst_bound = dst_existing >= 0;
```

**后果**：对每一个 tag 都把它的**全部 `HAS_INTEREST` 入边**扫一遍，
再用 Filter 丢掉非 `p` 的那些。`HAS_INTEREST` 平均入度只有 2.2，但热门 tag 可达 173，
而这一步在 18,321 个帖子上各发生一次。

**改动点（精确）**：让第二跳把「已知的 `p` 值」作为终点绑定传给 `Expand`
（即令 `dst_bound = true`），从而只保留指向 `p` 的那条边，而不是扫全入边再过滤。
`bindExistsSubPlan` 里 `saved_chain_corrs` 已经保存了该值所在的列（`saved_var` / `saved_col`），
所以数据是现成的，缺的是把它接到 `Expand` 的终点绑定上。


## D 方案的量化证据与实现障碍

### 量化：`Expand` 被逐帖调用 18,500 次

插桩 `ExpandPhysicalOp::executeChunk` 统计「调用次数 / 输入行数」后实测：

```
[EXPAND] chunks=18500 input_rows=18500        ← 平均每次调用只有 1 行输入
```

同一查询的耗时对照：

| 推导谓词 | 耗时 |
|---|---:|
| `sum(size(posts))`（无谓词、无 Expand） | 394 ms |
| `sum(size([x IN posts WHERE (x)-[:HAS_TAG]->()]))`（单跳、1 次 Expand/帖） | **4451 ms** |

**18,321 帖 → 约 18,500 次 `Expand` 调用，每次 1 行**，代价约 **240 µs/帖**。
而每帖平均只有 ~2 条 `HAS_TAG` 边 —— 所以这 4.45 s 买到的不是「扫了很多边」，
而是**固定开销**：每个帖子都把右子计划从头到尾执行一遍
（`Unwind → CorrelatedSource → Expand → Aggregate`）。

**这直接支持 D**：把子计划执行次数从 `帖数`(≈18,321) 降到 `组数`(171)，约 107 倍。

### 实现障碍（已确认，需架构改动）

批量化的卡点很具体 —— `PatternComprehensionApplyPhysicalOp::executeChunk` 在
**左行的循环体内**调用 `right_->executeChunk()`：

```cpp
for (size_t i = 0; i < n_left; ++i) {
    // 注入左行 i 的关联值
    correlated_source_->setValues(std::move(corr_values));
    std::vector<ListValue> collected;
    auto right_gen = right_->executeChunk();   // ← 每个左行一次完整子计划
    while (auto right_chunk = co_await right_gen.next()) { ... }
    ...
}
```

因此右子计划天然是**按左行的**，无法在不改这个算子契约的前提下批处理。

**另外**：仓库里**没有**批量展平算符（`src/query/physical_plan/operator/` 下无 `Unfold`/`unnest`）。
要做 D 需要：

1. 新增一个「把 list 列展平成多行（一次产出整块）」的算子（`Unfold`），取代子计划里的 `Unwind`；
2. 让 `PatternComprehensionApplyPhysicalOp` 对「列表在左行里、且需要逐元素展开」的推导
   **一次性把整批元素交给右子计划**，而不是逐左行调用 —— 即需要一种「左行携带列表、
   子计划批量消费」的关联形态（现有 `CorrelatedSource` 只按值注入，不带行身份）。

**建议的实现顺序**（降低风险）：

1. 先只支持**叶形态**：`size([x IN list WHERE (x)-[:R]->(y)])`（单跳、只消费计数、`list` 来自左行）；
2. 用 `exists`/`count` 语义替代 `collect`，避免构造整张列表；
3. 验证 complex-10 有量级收益后，再推广到两跳（`... <-[:R]-(p)`）与多跳。


## neo4j 是否有同样的优化？—— 测量对照

同一形状、同一数据（sf0.1，pid 933），两引擎逐项对照：

| 推导谓词 | eugraph | neo4j | 倍数 |
|---|---:|---:|---:|
| 无谓词（`sum(size(posts))`） | 394 ms | 128 ms | 3.1x |
| **单跳 `(x)-[:HAS_TAG]->()`** | **4451 ms** | **157 ms** | **28x** |
| 两跳 `(x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)` | 5851 ms | 216 ms | 27x |

**关键比值**：加一次单跳展开后，

* eugraph：394 → 4451 ms（**+4057 ms，11.3 倍**）
* neo4j：128 → 157 ms（**+29 ms，1.2 倍**）

即 **neo4j 几乎没有「逐帖固定成本」**。这与它的计划一致：推导**不作为独立算子出现**
（`EXPLAIN` 里 `OrderedAggregation` 之后直接是 `OptionalExpand`），
模式表达式在运行时**内联求值**，因此不存在「每帖重建一次子计划」的开销。

**结论：是的，neo4j 实质上避免了这一开销** —— 不是靠一个叫「批处理」的显式优化，
而是因为它的模式推导不编译成逐行的相关子计划。我们的 D 方案是**用批处理达到同样的效果**。

> 说明：以上是**测量与计划层面的对照结论**；neo4j 内部具体用哪种算子实现内联求值，
> 我没有从源码确认，不做断言。


## D 的实现形态（已定位到具体代码，尚未实施）

### 批量化的落点只在 Apply 一侧

一个重要事实：**`UnwindPhysicalOp` 本身已经是按块工作的** —— 它一次读入整个 chunk、
对 `chunk->count` 个列表逐个展开（`unwind_physical_op.cpp` 里的 `for (size_t r = 0; r < chunk->count; ++r)`）。
真正的限制是 **`PatternComprehensionApplyPhysicalOp` 每次只喂 1 行**，
所以 `Unwind` 永远只看到 1 行输入。

### 卡点的两处代码

```cpp
// 1) 输入侧：关联值按左行取
for (size_t i = 0; i < n_left; ++i) {
    std::vector<Value> corr_values;
    for (uint32_t col_idx : left_correlation_cols_)
        corr_values.push_back(in_chunk.getValue(col_idx, i));
    correlated_source_->setValues(std::move(corr_values));

    auto right_gen = right_->executeChunk();   // ← 每个左行一次完整子计划
    ...
    // 2) 输出侧：结果按左行下标写回
    list_buffers[oi]->list_data[i] = std::move(collected[oi]);
}
```

输入与输出**都绑定在「左行」上**，因此批量化需要改动这个契约，具体是：

1. **`Unwind` 一次收到整批列表**（不再每左行一次），因此子计划里
   **每个输出元素必须携带「它属于哪个左行」的行身份**，
   才能把结果写回 `list_data[i]`。现有 `CorrelatedSource` 只按**值**注入、不带行身份 ——
   这是需要新增的那部分（例如随元素一起传一个 `left_row` 列）。
2. **`Aggregate(collect)` 需要按行身份分组**（而不是当前的「每次执行只有一组」），
   即从 `keys=0` 变成 `keys=1`（按 left_row 分组）。
3. 结果从「每次执行返回 1 行 1 个列表」变成「返回 N 行、每行 (left_row, list)」，
   Apply 据此分发回 `list_data[left_row]`。

### 风险与建议顺序

这是一次**契约级重构**（改 Apply 的输入/输出语义 + 新增行身份列 + Aggregate 分组键），
不是局部优化，需要配套回归（`query_executor_tests` 里已有若干 PatternComprehension 用例，
以及 `PatternPredicateTwoNodes` / `NestedExistsWithCorrelatedPropertyFilter` 这类共享
`CorrelatedSource` 的 EXISTS 路径 —— 后者曾在类似改动中被打断过，必须一并验证）。

**建议**：先做一个**只服务叶形态**的窄实现并在其上验证量级收益，再决定是否推广：

* 识别 `size([x IN list WHERE (x)-[:R]->(y)])` 且只消费计数；
* 为这一形态生成「按 left_row 分组」的子计划；
* 用 `exists`/`count` 语义替代 `collect`（不构造整张列表，`existence_only` 是同思路先例）；
* 对照 complex-10 与 `PatternComprehensionApply` 相关单测，确认无回归后再推广到两跳/多跳。


## D 的收益上界被实测大幅下修（重要更正）

上一节据「18,321 帖 → 18,500 次调用、单跳 +4057 ms」推断 D 有 ~107 倍收益。
**对 `ExpandPhysicalOp` 做严格的内部计时后，这个推断被推翻**：

```
[T] calls=18000 avg_child_us=0.0 avg_total_us=35.3 sum_total_ms=634
```

| 项 | 值 |
|---|---:|
| 单跳查询总耗时 | **4717 ms** |
| `Expand` 累计耗时（含子生成器驱动） | **634 ms** |
| 占比 | **13%** |
| 调用次数 | 18000+ |
| 平均每次 | 35 µs |
| 每次的「建立开销」（`child_->executeChunk()` 创建） | **0.0 µs** |

**两个更正**：

1. **`Expand` 只占 13%**，所以「把 Expand 批量化」的收益上界是 **13%**，不是 107 倍。
   之前算出的「240 µs/帖」是**总耗时 ÷ 调用数**，把 Expand 之外的开销也算进去了 ——
   典型的把「总时间/次数」误当「单次成本」。
2. **每次调用的建立开销实测为 0** —— 我先后假设过「`allowed_filter` 索引扫描」与
   「子生成器构造开销」是主因，**两者都被实测否证**（前者 `filtered=0` 从未走到，
   后者 `avg_child_us=0.0`）。

**剩余 ~87%（约 4.1 s）不在 Expand 里**，而在推导子计划的其余部分：
`Unwind` 的逐元素驱动、`Aggregate(collect)` 的累加、以及 `PatternComprehensionApply`
对右子计划的逐左行驱动（18,321 次 `executeChunk()` 调用链）。

### 结论：优化目标应改为「减少子计划驱动次数」而非「批量化 Expand」

D 的原始动机（把子计划执行次数从帖数降到组数）**依然成立且是主要收益来源**，
但收益不是来自 Expand 本身，而是来自**少建/少驱动 18,321 次子计划管线**。
下一步应先度量这条驱动链（`PatternComprehensionApply` → `right_->executeChunk()`）的
累计开销，确认它就是那 87%，再决定重构范围。


## 完整归因链（最终）：88% 花在「每帖驱动一次子计划」

在 `PatternComprehensionApplyPhysicalOp` 里累计「驱动 `right_->executeChunk()` 并读完」的耗时：

```
[DRIVE] right_calls=18000  avg_us=243.4  sum_ms=4380
```

| 项 | 值 |
|---|---:|
| 单跳查询总耗时 | 4982 ms |
| **子计划驱动累计** | **4380 ms（88%）** |
| 驱动次数 | **≈ 18000**（= 帖数） |
| 每次驱动平均 | **243 µs** |

而这两条查询真正的「业务量」是：

| 量 | 值 |
|---|---:|
| 组数（`f`） | 171 |
| 帖数（`posts` 元素） | **18,321** |
| `sum(size(posts))` | 18,321 |

**即：为了算出 18,321 个计数，引擎执行了 ~18,000 次完整子计划
（`CorrelatedSource → Unwind → Expand → Aggregate`），每次 ~243 µs。**

### 三级归因汇总

| 层 | 耗时 | 占比 | 结论 |
|---|---:|---:|---|
| 纯 2 跳 VLE | 5.87 ms | 0.1% | 与瓶颈无关（最初误判为此） |
| `Expand` 累计 | 634 ms | **13%** | 批量化 Expand 收益上限仅 13% |
| **子计划驱动** | **4380 ms** | **88%** | **真正的瓶颈** |

### 修正后的优化目标

**不是「批量化 Expand」，而是「把每帖一次的子计划执行降为每组一次」。**

* 帖数 18,321 → 组数 171，即 **约 107 次子计划执行代替 18,321 次**；
* 收益上界从「13%」修正为**接近 88%**（4.38 s 中大部分可省）；
* 与 neo4j 的对照吻合：它的推导不产生独立算子（`EXPLAIN` 里直接内联求值），
  因此根本没有这 18,000 次子计划执行。

**实现仍按 `## D 的实现形态` 那三步**（左行身份列 → 按身份分组 collect → 结果回填），
但**动机与收益重新对齐到「减少驱动次数」**，而不是「让 Expand 处理更多行」。


## 实施评估：两条路线，建议改走「改写」而非「批量化」

排查批量化落地时确认了两个关键事实，它们改变了推荐方案。

### 事实一：`CorrelatedSourcePhysicalOp` 被 4 个算子共用

```
SemiJoinPhysicalOp          (EXISTS)
LeftJoinPhysicalOp          (OPTIONAL MATCH)
ApplyPhysicalOp
PatternComprehensionApplyPhysicalOp
```

改它的契约（增加行身份列、一次产出多行）会**波及 EXISTS 与 OPTIONAL MATCH 两条路径** ——
这正是此前打断 `PatternPredicateTwoNodes` / `NestedExistsWithCorrelatedPropertyFilter`
的那一层。风险不在推导本身，而在**共用件**。

### 事实二：批量化所需的行身份要穿透整条子计划

当前子计划（单跳 `sum(size([x IN posts WHERE (x)-[:HAS_TAG]->()]))`）：

```
Aggregate(keys=2, aggs=1)          ← 左侧分组（f, p）
  PatternComprehensionApply
    ...left...
    ── right subplan ──
    Aggregate(keys=0, aggs=1)      ← collect
      Filter
        Expand(src=x, dst=__anon, OUT, HAS_TAG)
          CorrelatedSource         ← 每次执行只给一个 f（=一个组）
```

要按组批量，必须让 `CorrelatedSource` **一次产出所有组的 (row_id, values)**，
于是 `row_id` 要一路穿透 `Expand` / `Filter` / `Aggregate`，
且 `collect` 要从 `keys=0` 变成按 `row_id` 分组。**这是 4+ 文件的契约级改动。**

### 更优路线：把推导「改写」为普通的按组聚合（建议）

观察：`sum(size([x IN posts WHERE (x)-[:HAS_TAG]->()]))` 的语义等价于

```cypher
MATCH (f)<-[:HAS_CREATOR]-(post:Post)-[:HAS_TAG]->()
RETURN sum(...)   -- 按 f 分组计数
```

即**从「逐帖执行子计划」改写为「一次扫描 + 按组聚合」**。
这在计划层是**标准的关系代数改写**（把相关子查询解相关 / de-correlate），
不必给 `CorrelatedSource` 加行身份：

* 列出 `posts` 的元素 → 一次 `Expand`（或直接把 `posts` 当作批量输入）；
* 按 **左行** 分组计数；
* 左侧 `f` 从「每行一个值」变成「一整批」—— 这正是普通 `Expand` 已经支持的批量形态。

**与 neo4j 的对照支持这条路线**：它的推导根本不作为算子出现
（`EXPLAIN` 里 `OrderedAggregation` 之后直接是 `OptionalExpand`），
也就是**它做的正是这种解相关改写**，而不是「批量化一个相关子计划」。

### 建议的实施顺序

1. 在 optimizer 加一条规则：识别「`size/collect` 的值只被 `size`/`sum(size(...))` 消费、
   且子计划为 `(x)-[:R]->(y)` 单跳」的推导，改写为**按左行分组的聚合**；
2. 先只覆盖单跳、且谓词里不引用 `p` 以外关联变量的情形；
3. 用 complex-10 与 `query_executor_tests` 里的 PatternComprehension 用例验证；
4. 收益确认后再推广到两跳。

**为什么这条更彻底**：它移除的是「相关子计划」这个**结构**，
而不是让这个结构跑得更快 —— 与 neo4j 的做法一致，也不会碰共用的 `CorrelatedSource`。


## 可复制的现成机制：`existence_only`（推荐按此实现 count 版）

排查中发现项目里**已经有**「按消费者语义改写推导子计划形状」的完整先例 —— `existence_only`：

| 环节 | 位置 |
|---|---|
| 语义标记 | `BoundPatternComprehensionApplyOp::existence_only`（`bound_pattern_comprehension_apply_op.hpp`）|
| 识别与设置 | `bind_return.cpp` ~852（判定「该推导只被 `size(list) > 0` 消费」）|
| 传递到物理层 | `physical_planner.cpp` ~3315 → `setExistenceOnly(v.existence_only)` |
| 生效 | `PatternComprehensionApplyPhysicalOp`：**找到第一条匹配即停**，并放一个占位元素 |

它的设计注释写得很清楚：*「every consumer of this column reads it solely through the
synthesised `size(list) > 0` predicate, so the list contents are dead — only its emptiness is
observable」* —— 即**列表内容已死，只留存在性**。

### 对 `sum(size(...))` 的对应实现（建议下一步）

同一思路可直接推广：**当列表只被 `size()` 消费时，列表内容同样已死，只需计数**。

* 识别：`size(<pc list>)` 或 `sum(size(<pc list>))` —— 与 `existence_only` 的识别位置相同（`bind_return.cpp`）；
* 标记：给 `BoundPatternComprehensionApplyOp` 增加 `count_only`；
* 生效：物理层**不做 `collect` 的列表构造**，只累加计数 ——
  即把子计划尾部的 `Aggregate(collect)` 换成 `Aggregate(count)`。

**为什么这条比批量化更值得先做**：

1. **不碰共用件**：`existence_only` 全程只新增标记，`CorrelatedSource` 契约不变，
   EXISTS / OPTIONAL MATCH 路径不受影响（而批量化要改共用的 `CorrelatedSource`）；
2. **有现成模板**：四处改动点与 `existence_only` 一一对应，形态已被验证可行；
3. **直击 88% 中的列表构造部分**：18,321 个 `collect` 只为取 `size`，
   列表本身是纯粹浪费；`existence_only` 已经证明「跳过列表构造」这条路在本引擎可行。

**注意**：这条**不解决**「每帖驱动一次子计划」的固定成本（那需要批量化/解相关），
但它**风险最低、且与本轮已确认的证据直接对应**，适合作为第一步。
之后再做解相关改写以消除那 18,000 次驱动。


## `count_only` 的精确改动点（已定位到行，尚未实施）

### 为什么不能在物理层做（重要更正）

最初设想「在 `PatternComprehensionApplyPhysicalOp` 里取 `elements.size()` 而不构造列表」——
**该设想无效**：到达 Apply 时，子计划尾部的 `Aggregate(collect)` **已经把列表建好了**，
再取 `.size()` 省不下任何东西。**必须改降级层**，让子计划直接产出计数。

### 改动点一：降级层把 `collect()` 换成 `count()`

`src/query/planner/binder/bind_match.cpp:1884`（`bindPatternComprehension` 内）：

```cpp
const function::FunctionDef* collect_fn = func_registry_.lookup("collect", {out_element_type});
...
agg_item.func_def = collect_fn;
agg_item.function_name = "collect";
agg_item.arguments.push_back(BoundExpression(BoundColumnRef(0, out_element_type, "__pc_proj", INVALID_SLOT_ID)));
agg_item.alias = "__pc_list";
agg_item.result_type = BoundType::List(out_element_type);
agg_item.is_visible = false;   // ← 注释说明「PCApply reads this column directly」
```

改为 `lookup("count", {out_element_type})`、`result_type = BoundType::Int64()`、
别名如 `__pc_count`。**这样才真正跳过列表构造。**

### 改动点二：Apply 的列契约

`PatternComprehensionApplyPhysicalOp` 当前**期望该列是 `ListValue`**
（`std::holds_alternative<ListValue>(v)` → `collected[oi] = std::move(lv)`）。
改后该列是 `int64`，需要：
* 要么在 Apply 里识别 `int64` 并**合成一个长度正确的 ListValue**
  （`list_buffers[oi]->list_data[i]` 需要一个 `ListValue`，而下游只有 `size(...)` 在读它 ——
  但**合成 N 个占位元素本身也有成本**，需实测是否仍划算）；
* 要么让 Apply 支持「列语义为 COUNT」的输出，并让下游 `size()` 直接读该计数
  （需要 `BoundPatternComprehensionApplyOp::Output` 增加「这是计数而非列表」的标记，
  并让 `size(<pc>)` 的绑定走计数路径）。

**第二条更彻底**（真正零列表），但需要动 `size()` 的绑定；第一条更窄但收益可能被合成抵消。
**建议先实测第一条**：若 18,321 次合成仍远快于构造 18,321 个真实列表，就先落地窄版本。

### 验证方案

1. **语义对照**：`size([x IN posts WHERE (x)-[:HAS_TAG]->()])` 的逐组值，
   改动前后必须完全一致（用 `WITH f, collect(post) AS posts, p RETURN f.id, size([...])` 对照）；
2. **complex-10**：逐行 `score = 2*cpc - pc` 与基础 `MATCH` 重算一致（现有验收口径）；
3. **回归**：`query_executor_tests`（含 `PatternComprehension*` 与
   `ListComprehensionPatternPredicate*` 用例）、`optimizer_tests`；
4. **性能**：单跳查询 4982 ms → 目标量级下降（理论上省掉 18,321 次列表构造；
   每帖驱动子计划的固定成本仍在，故不会降到 neo4j 的 157 ms）。

### 仍未解决的部分（诚实标注）

本改动**不消除**「每帖驱动一次子计划」的 88% 固定成本。
要消除它需要 `## 实施评估` 里讨论的**解相关改写**（把推导改为按组聚合，
不碰共用的 `CorrelatedSource`）。两步是独立的，`count_only` 是低风险的第一步。


## `count_only` 实施进展与剩余冲突（本轮实测）

### 已完成并通过验证的部分

| 改动 | 位置 | 状态 |
|---|---|---|
| 降级层支持 `count_only`（`collect`→`count`，输出 INT64、别名 `__pc_count`） | `bind_match.cpp`（`bindPatternComprehension`） | ✅ 默认 false，无行为变化 |
| 签名与语义文档 | `binder.hpp` | ✅ |
| Apply 接受 INT64 计数列（按该长度合成占位列表，维持 `ListValue` 契约） | `pattern_comprehension_apply_physical_op.cpp` | ✅ |
| 检测「只被 `size()` 消费」的推导 | `bind_return.cpp`（`collectSizeOnlyPatterns`） | ⚠️ 见下 |

全程 `query_executor_tests` **524/524**、`optimizer_tests` **109/109**、格式通过。

### 剩余冲突：`existence_only` 抢先匹配

插桩输出：

```
[SIZE] pc_asts=1  size_only=0  existence=1
```

即目标推导被 **`existence_only`** 先认领了。原因：LDBC 写法
`size([x IN posts WHERE (x)-[:HAS_TAG]->()])` 的参数是 **`ListComprehension`**，
其 `where_pred` 是**裸模式谓词**（parser 转成 `ExistsExpr`），
而 `collectExistenceDerivedPatterns` 正是收集这类 `ExistsExpr` 的 ——
于是 `count_only` 的判定 `!existence_only.count(pc) && size_only.count(pc) > 0` 恒为 false。

**这不是 bug，而是一个语义分层问题**：同一个节点在两种消费者下含义不同 ——
* 作为布尔谓词（`WHERE (a)-[:R]->(b)`）时，只有**存在性**可观测 → `existence_only` 正确；
* 作为 `size(<list>)` 的参数时，**元素个数**可观测 → 应当是 `count_only`。

**修法方向**：判定顺序应让 **`size(...)` 上下文优先**，即
「若该推导位于 `size()` 的实参内，则它是 count_only，即使其 AST 节点是裸 `ExistsExpr`」。
具体要在 `collectExistenceDerivedPatterns` 的 walk 里**跳过 `size(<ListComprehension>)` 的实参**
（那里不应产生 existence 语义），改由 `collectSizeOnlyPatterns` 认领。

### 下一轮的最小改动

1. 在 `collectExistenceDerivedPatterns` 的 `FunctionCall` 分支里，遇到 `name == "size"` 时
   **不再向下 walk 其参数**（该上下文的消费者是 size，不是布尔判断）；
2. 这样同一个推导只被 `collectSizeOnlyPatterns` 收集，`count_only` 生效；
3. 验证：计划中出现 `__pc_count`、单跳查询耗时下降、complex-10 逐行分数不变、
   524/524 与 109/109 保持。


## `count_only`：最后一环的准确位置（LDBC 形状）

补上 `existence_only` 的 `size()` 守卫后（不再下探 `size()` 实参），**LDBC 形状仍未生效**。
查清了原因 —— 该形状走的是**另一条绑定路径**，`count_only` 没有接上去。

### 两条路径的区别

| 形状 | 处理路径 | `count_only` 是否接上 |
|---|---|---|
| `size([(x)-[:R]->(y) \| 1])`（直接模式推导） | `hoistPatternComprehensions`（`bind_return.cpp:944-978`） | ✅ 已接 |
| `size([x IN posts WHERE <裸模式>])`（**LDBC 写法**） | `lowerListComprehensionWithPatternComprehension`（`bind_return.cpp:1227`） | ❌ **未接** |

**为什么未接**：后者在 `bind_return.cpp:1878-1914` 发现 `pc_asts` 并调用
`lowerListComprehensionWithPatternComprehension`，而该函数内部**并不直接调用
`bindPatternComprehension`** —— 它通过 `bindExpression` 绑定
`lc.where_pred`，`bindExpression` 遇到 `cypher::PatternComprehension` 时只产生
**占位**（`binder/bind_expression.cpp:702` 的 `BoundPatternComprehension`），
真正的 `bindPatternComprehension` 调用发生在**后续的 EXISTS/子计划构造**里
（`bindExistsSubPlan` 路径）。

因此 `count_only` 无法从 `lowerListComprehensionWithPatternComprehension` 直接传下去，
需要**绑定期上下文**：让 `bindExpression`（或 hoisting）在「当前处于 `size()` 实参内」
时置一个标志，供 `bindPatternComprehension` 读取。

仓库里**目前没有**这种表达式级上下文标志机制（`binder.hpp` 内无 `in_*` / context flag 成员），
所以这一步是**新增一个机制**，不是接线。

### 建议实现（下一轮）

1. 在 `Binder` 加一个计数器成员，如 `int size_context_depth_ = 0;`
   （`bindExpression` 处理 `FunctionCall` 且 `name == "size"` 时 `++`，返回时 `--`，RAII 或手动配对）；
2. `bindPatternComprehension` 在有该标志时按 count-only 处理
   （等价于 `count_only = true`）；
3. 与现有 `size_only` 集合去重，避免双重判定；
4. 验证：计划出现 `__pc_count`、单跳查询耗时下降、complex-10 逐行分数不变
   （`score = 2*cpc - pc` 与基础 `MATCH` 一致）、524/524 与 109/109 保持。

### 本轮已落地（可复用）

* `count_only` 降级分支（`bind_match.cpp`）、签名与语义文档（`binder.hpp`）、
  Apply 的 INT64 计数列支持（`pattern_comprehension_apply_physical_op.cpp`）；
* `collectSizeOnlyPatterns` 检测（覆盖直接形状 + LDBC 形状的 AST 发现）；
* `existence_only` 收集器不再下探 `size()` 实参（语义分层修正）。

全部改动 `query_executor_tests` **524/524**、`optimizer_tests` **109/109**、格式通过。


## `size_context_depth_` 已实现但未生效：机制仍未命中入口

按上节方案实现了表达式级上下文标志：

* `Binder::size_context_depth_`（计数而非布尔，因为 `size()` 会嵌套），
  由 `bindExpression` 在绑定 `FunctionCall` 且 `name == "size"` 时经 RAII 守卫增删；
* `bindPatternComprehension` 在该值 > 0 时置 `count_only = true`。

**结果：仍未生效**（计划里 `__pc_count` 出现 0 次；单跳查询 5116 ms，与改动前同量级）。
全部改动保持 `query_executor_tests` **524/524**、`optimizer_tests` **109/109**、格式通过。

**由此确认的关键事实**：LDBC 形状的推导**不经过 `bindPatternComprehension`**。
该形状是 `size([x IN posts WHERE <裸模式谓词>])` ——
`where_pred` 里的裸模式谓词是 **`ExistsExpr`**，它由 **`bindExistsSubPlan`** 处理
（`bind_match.cpp:1146` 起有专门的 `exists.is_bare_predicate` 分支）。
`bindPatternComprehension` 只在**直接**模式推导（`size([(x)-[:R]->(y) | 1])`）路径上被调用。

### 因此入口应在 `bindExistsSubPlan`

`bindExistsSubPlan` 才是 LDBC 形状真正构建子计划的地方，它已经：
* 知道 `exists.is_bare_predicate`；
* 有 `saved_ctx` / `correlation` / `ctx_` 可供判断「该子计划的结果是否只被 size 消费」。

**下一轮的正确落点**：在 `bindExistsSubPlan` 里，当
`size_context_depth_ > 0 && exists.is_bare_predicate` 时，
**把子计划尾部的 `collect` 换成 `count`**（即在 `bindExistsSubPlan` 内部或它对
`BoundPatternComprehensionApplyOp` 的封装处完成聚合算子的选择），
而不是在 `bindPatternComprehension` 里。

`size_context_depth_` 这个标志本身可以直接复用（已验证正确维护、无回归）。

### 本轮完整产出

| 改动 | 状态 |
|---|---|
| `count_only` 降级分支（`bind_match.cpp`，`collect`→`count`／`__pc_count`／INT64） | ✅ 已提交 |
| 签名与语义文档（`binder.hpp`） | ✅ 已提交 |
| Apply 支持 INT64 计数列（合成等长占位列表） | ✅ 已提交 |
| `collectSizeOnlyPatterns` 检测（两种形状 + 裸 `ExistsExpr` intern） | ✅ 已提交 |
| `existence_only` 不下探 `size()` 实参（语义分层修正） | ✅ 已提交 |
| `Binder::size_context_depth_` + `bindExpression` 守卫 | ✅ 本轮，未生效 |

**未完成**：把 `count` 的聚合选择接到 `bindExistsSubPlan`（LDBC 形状的真实入口）。


## 决定性证据：`bindPatternComprehension` 不是这个推导的创建者

上一节推断「入口应在 `bindExistsSubPlan`」，但先做了插桩验证，结果推翻了它 ——
**`bindPatternComprehension` 确实被调用、且 size 上下文已正确置位，但目标推导的聚合不在这里选**：

```
[CTX] bindPatternComprehension: size_depth=1 count_only_in=0    ← 上下文标志已生效
__pc_count = 0                                                   ← 但 count 未启用
```

即：把 `size_context_depth_` 在降级期间抬起来之后（改动 `bind_return.cpp` 的
`lowerListComprehensionWithPatternComprehension` 调用点），`bindPatternComprehension`
**确实**在 `size_depth=1` 下被调用，`count_only` 也置为 true ——
**但最终计划里没有 `__pc_count`**，说明该推导的 `Aggregate` **不是**由
`bindPatternComprehension` 构造的（它内部会加 `Project + Aggregate(collect)`）。

**因此真正创建该 Apply 与其聚合的是另一条路径**，尚未定位到。
本轮所有尝试都停在「找到了调用者、但调用者不是创建者」这一层。

### 另需注意：一个我引入的性能副作用

上面的改动同时让 Apply 的 INT64→列表合成分支对这条查询生效（白做合成），
单跳查询一度从 ~5.1 s 升到 ~8.2 s。**该改动已 stash 撤回**，
代码回到 `5331d44b` 的已提交状态（`query_executor_tests` 524/524、
`optimizer_tests` 109/109、格式通过）。

### 保留的教训（值得写进方法）

**连续四轮选错了入口**（`hoistPatternComprehensions` →
`lowerListComprehensionWithPatternComprehension` → `bindPatternComprehension` →
`bindExistsSubPlan`），每次都靠插桩纠正。**根因是：我从代码结构推断「谁创建了这个算子」，
而没有先插桩确认创建点。** 正确做法与后来对 `Expand` 做的一样：
**先在算子构造函数或创建语句处插桩，确认调用栈，再决定改哪里。**

### 下一步（唯一未验证的观测）

在 `BoundPatternComprehensionApplyOp` 的**创建点**插桩（而非猜测调用者），
打印创建时的调用栈/入口函数名，确认这条路径，再决定 `count` 聚合选择接在哪里。


## 创建点插桩：创建者是两个绑定点之一，且创建时 size 上下文未抬起

按「先插桩创建点、再改代码」的方法，在 `BoundPatternComprehensionApplyOp` 的
**全部三个创建点**插桩（两个绑定点 + 优化器 memo 拷贝），实测：

```
[CREATE] bind_match.cpp:1945   size_depth=0
[CREATE] bind_return.cpp:1424  size_depth=0
[CREATE] optimizer/memo.cpp    (拷贝 ×2)
```

**结论**：

1. **创建者必是这两个绑定点之一**（`memo.cpp` 只是拷贝）——
   这把「入口在哪」从四个候选收窄到两个确定位置；
2. **两个创建点都在 `size_depth=0` 时创建** —— 说明**创建路径上 size 上下文从未抬起**。
   也就是说，`bindExpression` 里的 size 作用域守卫（`5331d44b` 已提交、实测正确置位）
   **与创建 Apply 的路径不重叠**：守卫在绑定表达式期间生效，而 Apply 的创建发生在
   之后的降级/提升阶段，那时守卫早已恢复。

### 因此修法明确：在**创建路径上**抬起上下文

不需要新的机制，只需把已有的 `size_context_depth_` 在**创建点所在的那条路径**上置位。
两个候选创建点各自对应一条路径：

| 创建点 | 所在函数 | 何时被调用 |
|---|---|---|
| `bind_match.cpp:1945` | `bindPatternComprehension` | 由其调用者（hoisting / 降级）触发 |
| `bind_return.cpp:1424` | `lowerListComprehensionWithPatternComprehension` | 由 `bindReturn` / `bindWith` 的降级分支触发 |

**下一步的最小实验**：在这两个创建点打印**调用者**（或直接在各自的入口打印
`size_context_depth_` 与 `__func__`），确认是哪一条在建，然后在那条路径上置位。

### 本轮方法上的改进（按「性能优化经验」）

前四轮失败的原因是**从代码结构推断创建者**。本轮改为**在创建点插桩**
（与之前定位 `Expand` 热点时用的方法一致），一次就把候选从四个收窄到两个，
并测出「创建时上下文未抬起」这一关键事实 —— **观测取代推断**再次奏效。


## `count_only` 已生效（实测确认），但加速被掩盖

用创建点/聚合选择点插桩，确认优化**真正生效**：

```
[CREATE] bind_match.cpp:1945 depth=1        ← 创建时 size 上下文已抬起
[AGG] choosing count (count_only=1 depth=1)  ← 聚合确实选择了 count
结果值 = 3640                                ← 与改动前一致，语义正确
```

**关键**：`__pc_count` 未出现在计划 dump 里，曾被误判为「未生效」——
实际是**计划 dump 不展开内部聚合列名**，而聚合选择点插桩证明 `count` 已被选中。
**这是一个测量方法问题，不是功能问题。**

### 性能：被机器方差与主导成本掩盖

同一查询多次测量的**跨度很大**（5116 / 6617 / 8176 / 8392 ms），
而「无谓词」基线也从 757 到 1280 ms 波动 —— 本机在该时段处于**降频/节流**状态
（`query_executor_tests` 总时长从 92 s 涨到 169 s，同为佐证）。

| 项 | 值 |
|---|---:|
| 无谓词（无推导展开） | 757 ms |
| 单跳（count_only 生效） | 6617 ms（跨轮 5116–8392） |

**结论（诚实）**：`count_only` 消除了 18,321 次列表构造（语义等价、测试全绿），
但**没有带来可测量的端到端加速** —— 因为主导成本是那 **88% 的「每帖驱动一次子计划」**
（18,321 次 × ~243 µs ≈ 4.4 s），列表构造只是其中的小头。

**下一步仍有价值的是解相关改写**（把子计划执行次数从帖数降到组数），而不是继续
在聚合形态上优化。本轮的价值在于：`count_only` 机制完整落地并**验证生效**，
同时用插桩纠正了「未生效」的误判。

### 方法教训（第二次同类）

本轮再次因为**只看计划 dump 就判断功能未生效**而多花了一轮。
正确做法是**在决策点插桩**（此处是聚合选择处），
这与之前「在创建点插桩」是同一条经验：**观测决策点，而不是观测最终产物**。


## `count_only` 的 A/B 结论：无可测收益（诚实收尾）

用同一二进制 + 环境变量开关（`EUGRAPH_NO_COUNT_ONLY`）做**三轮交错 A/B**，
每轮交替启停服务、各 5 次测量：

| 轮次 | count_only ON（min / med） | count_only OFF（min / med） |
|---|---|---|
| 1 | 6683 / 6741 ms | 6447 / 6820 ms |
| 2 | **6549 / 6632 ms** | 6582 / 6964 ms |
| 3 | 6778 / 6877 ms | **6404 / 6531 ms** |

**结论：无可测差异**（方向在轮次间翻转，差幅均在噪声内），两者结果值都是 `3640`（语义一致）。

### 为什么没有收益

`count_only` 消除了 **18,321 次列表构造**，但列表构造只是那 4.4 s 里的小头 ——
主导成本是**每帖驱动一次完整子计划**（18,321 次 × ~243 µs，已测），
其中包括 `CorrelatedSource` 注入、`Unwind` 驱动、算子链的协程调度。
**省掉 collect 的搬运动作，省的正是小头。**

### 处置

`count_only` 保留在代码里：**语义等价、测试全绿（524/524 + 109/109）、性能中性**，
且为后续「按组聚合」的解相关改写提供了现成的降级分支（`count` 聚合形态）。
临时 A/B 开关已移除。

### 真正的加速点仍是解相关

要把 18,321 次子计划执行降到 171 次（按组），必须改**执行结构**而非聚合形态。
本轮已把「谁创建 Apply」「聚合在哪选」两个位置都定位清楚：
`bind_match.cpp` 的 `bindPatternComprehension`（创建点）与同文件的聚合选择处，
解相关改写可以从这两处入手。


## 采样剖析：瓶颈是堆分配（计时插桩看不到的层面）

前面的逐算子计时把成本归到「驱动子计划」，但**看不到成本内部是什么**。
改用 `perf record -g -p <server pid>` 采样（同一查询，pid 933，单跳）：

| 符号 | 占比 |
|---|---:|
| `_Znwm`（`operator new`） | **28.6%** |
| `__libc_malloc` | **26.9%** |
| `_int_free_chunk` | **24.6%** |
| `_M_deallocate_nodes` / `_M_deallocate_node`（inlined） | 16.6% / 13.0% |
| `_int_malloc` / `_int_free_merge_chunk` | 14.5% / 12.6% |
| `~vector` / `~pair` / `_Destroy<optional<variant<…>>>` | 9.7% / 9.7% / 9.4% |
| **`VertexValue::VertexValue(const&)`（拷贝构造）** | **8.4%** |

**分配与释放合计约 80% 的采样**，且销毁的正是
`std::vector<std::optional<std::variant<monostate, bool, int64, double, string, …>>>`
—— 即 **`VertexValue` 的属性容器**，成对出现 `~pair` / `~vector` / `_Destroy`。

### 含义：`VertexValue` 被大量拷贝/销毁

这与「逐帖执行子计划」是**同一件事的两个视角**：
每个帖子的匹配过程都在**物化 `VertexValue`（整点属性）并拷贝它**，
每个拷贝都伴随一次属性容器的堆分配。

**因此真正的优化方向不是「批量化 Expand」，而是「不要在只计数时物化整点」**：

* 计数只需要「该点是否存在满足条件的邻居」，**不需要它的属性**；
* 当前路径却把点升级为 `VertexValue`（走 Enricher/构造），
  于是产生了 80% 的分配/释放开销；
* `count_only` 已经取消了**列表**构造，但**点物化**仍在 —— 这是下一个高价值目标。

### 建议下一步

1. 在 `perf` 采样基础上确认 `VertexValue` 构造的**调用来源**
   （`perf report -g graph` 或对 `VertexValue::VertexValue` 加计数插桩），
   确认是 Enricher、`ConstructVertex` 投影，还是推导子计划里的某处；
2. 若确认「只为计数而物化」，则让推导在**拓扑层**完成判定
   （`VertexRef` + 邻接扫描），避免升级为 `VertexValue`；
3. 这一项的收益上界远高于批量化 —— 它直接对应 80% 的采样。

### 方法教训（本轮第三次同类）

* **逐算子计时**只能告诉你「哪个算子花时间」，**采样剖析**才能告诉你「时间花在什么操作上」；
* 本轮若只用计时，会继续在「Expand / 子计划驱动」层面优化；
  采样一上来就指出**堆分配**，方向立刻不同。
* 这条与之前两条（创建点插桩、决策点插桩）同属一条经验：
  **选对观测手段，比更努力地推理更有效。**


## `VertexValue` 构造的代码级定位

按采样结论继续下钻，在源码中定位了「整点物化」的两处结构，均在
`src/query/physical_plan/operator/projection_extract_physical_op.cpp`
（`ProjectionExtractPhysicalOp::executeChunk`）：

| 行 | 结构 | 说明 |
|---|---|---|
| ~255 | `std::vector<std::vector<std::optional<VertexValue>>> vertex_ctor_cache(n_specs);` | 按 spec 批量预取整点属性 |
| ~350 | `std::unordered_map<size_t, VertexValue> vertex_obj;` | **每行一份**的 `source_col → 已构造 VertexValue` |
| ~352 | `std::unordered_map<size_t, EdgeValue> edge_obj;` | 同理，边的物化 |

`VertexValue` 的定义（`src/query/dataset/row.hpp:19`）是：

```cpp
struct VertexValue {
    VertexId id = INVALID_VERTEX_ID;
    std::unordered_map<LabelId, Properties> properties;   // ← 深拷贝成本在这里
    std::optional<LabelIdSet> labels;
    bool deleted = false;
};
```

**与采样的对应关系**：

* `properties` 是 `unordered_map` → 拷贝/析构即**整张属性表的深拷贝与节点释放**，
  对应采样的 `_M_deallocate_nodes` / `_M_deallocate_node` / `~pair` / `~vector` / `_Destroy<optional<variant>>`；
* `VertexValue::VertexValue(const&)` 占 **8.4%** 采样，说明**构造后还被大量拷贝**；
* `~vector`（9.7%）与 `_Destroy<optional<variant<…>>>`（9.4%）说明销毁的正是属性容器。

**因此「只为计数而物化整点」的链条是**：
推导谓词里的 `(x)-[:HAS_TAG]->()` 需要在每个 `x` 上做邻接判定，
但路径把 `x` 升级成了带完整属性表的 `VertexValue`（`ConstructVertex` 投影），
于是**每个帖子**都付出一次属性表构造 + 后续拷贝 + 析构。

### 下一步（收益直指 80% 采样）

1. 确认这条查询的计划里**哪些 `ProjectionExtract` 带 `ConstructVertex` spec**
   （`EUGRAPH_PLAN_DEBUG` 的计划 dump 会打印 spec 列表，可直接看）；
2. 若推导子计划内存在仅为邻接判定而构造的 `ConstructVertex`，
   让它在拓扑层用 `VertexRef` 完成判定，**不升级为 `VertexValue`**；
3. 复测采样，确认 `_Znwm` / `_M_deallocate_nodes` / `VertexValue(const&)` 占比下降。


## 最终归因：为「从不读取的属性」物化了整张属性表

把采样结论与计划对照后，做了一个**判别性测量**（同一形状，两个都不读属性的变体）：

| 查询 | 耗时 | 说明 |
|---|---:|---|
| `sum(size(posts))`（完全不引用 `post`） | **366 ms** | 基线 |
| `sum(size([x IN posts WHERE (x)-[:HAS_TAG]->()]))`（谓词只用邻接，**不读任何属性**） | **3826 ms** | **+3460 ms** |

**两者的 Cypher 层面都不读取 `x` / `post` 的任何属性**，但后者慢 **10 倍**。

对照计划，子计划左侧的 `ProjectionExtract` 里带着：

```
ProjectionExtract(specs=[f<pass>, __pe_…51<ctor-vertex>, __anon_edge_0<pass>,
                         post<pass>, __pe_…49<ctor-vertex>])
```

即 **`post` 列上有一个 `ctor-vertex` spec** ——
引擎在每个帖子上**构造完整 `VertexValue`（含 `unordered_map<LabelId, Properties>` 属性表）**，
而这条查询**从不读它的属性**。

**这就是采样的那 80%**：

* 每个帖子一次属性表构造（`_Znwm` 28.6% + `__libc_malloc` 26.9%）；
* 构造后还被拷贝（`VertexValue::VertexValue(const&)` 8.4%）；
* 随后整表析构（`_M_deallocate_nodes` 16.6% + `_int_free_chunk` 24.6% +
  `~vector` 9.7% + `_Destroy<optional<variant<…>>>` 9.4%）。

### 完整的归因链（本轮方法）

| 步骤 | 手段 | 得到的结论 |
|---|---|---|
| 1 | 逐算子计时 | `Expand` 只占 13%；**88% 在「每帖驱动一次子计划」** |
| 2 | `perf record -g` 采样 | 80% 采样在**堆分配/释放**，且销毁的是 `VertexValue` 的属性容器 |
| 3 | 源码定位 | 物化点在 `projection_extract_physical_op.cpp`（`vertex_ctor_cache` / `vertex_obj`） |
| 4 | **计划 + 判别测量** | `post` 列带 `ctor-vertex`，而查询**不读其属性** → **3460 ms 纯属浪费** |

### 下一步（收益最明确的一项）

**让「只为邻接判定/仅为传递」的列不做 `ctor-vertex`**：

* 计划里 `post<pass>`（拓扑引用）与 `__pe_…49<ctor-vertex>`（完整对象）**同时存在**，
  说明要求收集阶段把 `post` 判成了「需要整个对象」；
* 若该变量**没有任何属性被读取**（本例正是如此），只需 `VertexRef` 或空属性表的
  `VertexValue`，不必加载属性表；
* 落点：`src/query/optimizer/requirement_collector.cpp`（`need_entire` 的设置）
  与 `ProjectionExtract` 的 `ConstructVertex` 分支 ——
  让 `ConstructVertex` 在「无属性需求」时**跳过属性加载**。

**预期收益**：直接对应本次采样中 80% 的分配开销与那 3460 ms 中的大部分。


## 最终定位：`Decide` 给仅需拓扑引用的变量分配了 object slot

沿「`post` 列为何带 `ctor-vertex`」继续下钻，结果**排除**了要求收集：

插桩 `requirement_collector.cpp` 里 `need_entire = true` 的设置点（投影中的裸列引用），
跑目标查询得到 **零次触发** —— 即 **`need_entire` 根本没被设置**，
那条 `ctor-vertex` spec **不是由要求收集产生的**。

真正的产生点是 `src/query/physical_plan/physical_planner.cpp`：

```cpp
// ~219
if (pi.object_slot_id != binder::INVALID_SLOT_ID && pi.object_slot_id != pi.source_slot_id) {
    ColumnSpec s;
    s.kind = col_is_edge ? ColumnSpec::Kind::ConstructEdge : ColumnSpec::Kind::ConstructVertex;
    ...
}
```

即：**对象列的 emit 条件**是「`Decide` 为该变量分配了一个与源槽不同的 object slot」。
注释也写明了这一点：

> *Object column (Construct). Emitted iff Decide allocated a fresh slot
> (object_slot_id != source_slot_id).*

（另有 `~358` 一处，针对 `fresh_expands` 的同类补全。）

**因此链条是**：

```
Decide（column_rewrite）认为 post 需要「整对象」
  → 分配独立 object_slot_id
  → physical_planner 发出 ConstructVertex spec
  → ProjectionExtract 在每个帖子上构造完整 VertexValue（含属性表）
  → 拷贝 + 析构 → 采样中 80% 的分配开销
```

而这条查询**从不读 `post` 的任何属性** —— 该补全是纯粹的浪费。

### 下一步落点（已收窄到一个判断）

**`Decide` 为何判定 `post` 需要整对象**，是唯一未解的一环。需要看：

1. `src/query/optimizer/column_rewrite.*` 里 `Decide` / `object_slot_id` 的分配条件；
2. 该条件是否只依据 `need_entire`（已被排除）还是别的信号
   （如 `need_props` 非空、或「投影里出现该变量」本身）；
3. 若条件过宽，收紧为「确实有属性需求或确实要序列化整个对象时才补全」。

**预期收益**：直接消除那 3460 ms 中的大部分与采样里 80% 的分配/释放。


## 根因确认：`need_whole_vertex=1` 但属性需求为 0

在 `column_rewrite.cpp` 的决定点插桩（`~1727`，`Decide` 生成 PEPlan 处）：

```
[PE] slot=1 need_whole_vertex=1 need_whole_edge=0 nprops=0
[PE] slot=2 need_whole_vertex=1 need_whole_edge=0 nprops=0
[PE] slot=4 need_whole_vertex=1 need_whole_edge=0 nprops=0
[PE]   -> whole object requested, source_already_object=0      (×3)
```

**三个槽都请求整点，而 `vertex_props` 为空（`nprops=0`）**，且源不是已构造对象
（`source_already_object=0`）→ 分配新 `object_slot_id` → 发出 `ConstructVertex`。

**即：引擎为「不需要任何属性」的变量构造了完整 `VertexValue`** ——
它的 `properties` 映射是空的，但构造/拷贝/析构的固定成本照付。

### 完整归因链（终点）

| 环节 | 位置 | 事实 |
|---|---|---|
| 1 | `requirement_collector.cpp` | `need_whole_vertex` 被置位（`need_entire` 未被设置，**已排除**）|
| 2 | `column_rewrite.cpp:1727` | `need_whole_vertex=1` 且 `nprops=0` → 仍分配 `object_slot_id` |
| 3 | `physical_planner.cpp:219` | `object_slot_id != source_slot_id` → 发出 `ConstructVertex` |
| 4 | `projection_extract_physical_op.cpp` | 每个帖子构造完整 `VertexValue` + 拷贝 + 析构 |
| 5 | `perf` 采样 | **80% 采样在分配/释放**；判别测量显示 **+3460 ms** |

### 修法（最小且直接）

**在发出 `ConstructVertex` 前判断「是否真的需要属性」**：
若该变量的 `vertex_props` 为空、且下游不把它当整体对象序列化，
则**不构造 `VertexValue`**，直接沿用拓扑列（`VertexRef`）。

候选落点两处（择一或并用）：

1. `column_rewrite.cpp:1727`：要求为「整点」但 `vertex_props` 为空时，
   让 `object_slot_id = source_slot_id`（复用拓扑列），从而 planner 跳过 emit；
2. `physical_planner.cpp:219`：emit 条件再叠加「该变量确有属性需求」。

**注意**：`RETURN n`（真要把整个点交给客户端）必须保留补全 ——
判别条件是「是否存在真正的整对象消费」，而不是简单地取消 `need_whole_vertex`。

**预期收益**：消除采样中 80% 的分配/释放与判别测量里 3460 ms 的绝大部分。


## 最终根因：`BoundVariableRef` 把推导变量标记为需要整点

在每个 `need_whole_vertex = true` 设置点插桩（并先修正了一处插桩引入的
`else if` 括号缺陷 —— 在 `else if` 与赋值之间插入语句会让赋值变成无条件执行，
这是个真实陷阱），得到**唯一触发点**：

```
[WV] BoundVariableRef vertex name=f
[WV] BoundVariableRef vertex name=p
[WV] BoundVariableRef vertex name=post      ← 目标
```

即：**变量在表达式里以 `BoundVariableRef`（顶点类型）出现时，就被标记为需要整点**，
位置在 `column_rewrite.cpp` 的 `BoundVariableRef` 分支（~684）。

### 关键：代码里已有同类问题的现成机制

同一函数上方紧邻的分支写着：

```cpp
// List comprehension introduces a fresh per-iteration variable;
// update current_skip so child collection does not flag the
// loop variable as needing whole-object materialization.
current_skip = ptr->variable;
```

**`current_skip` 正是为「推导引入的循环变量不应触发整点物化」而存在的机制** ——
但它的覆盖范围只到「列表推导自己的循环变量」，**没有覆盖本例**：
本例的推导变量 `post` 来自**外层 `WITH ... collect(post) AS posts`**，
它是外层变量被推导复用的情形。

### 完整归因链（终点）

| # | 位置 | 事实 |
|---|---|---|
| 1 | `column_rewrite.cpp:~684` | `BoundVariableRef` 顶点引用 → `need_whole_vertex = true`（**实测唯一来源**）|
| 2 | `column_rewrite.cpp:~1727` | 该要求 + `nprops=0` → 分配 `object_slot_id` |
| 3 | `physical_planner.cpp:~219` | slot 不同 → 发出 `ConstructVertex` |
| 4 | `projection_extract_physical_op.cpp` | 每帖构造完整 `VertexValue` + 拷贝 + 析构 |
| 5 | `perf` + 判别测量 | **80% 采样在分配/释放**、**+3460 ms** |

### 修法（已收窄到一处判断）

在 `column_rewrite.cpp` 的 `BoundVariableRef` 分支，**扩展 `current_skip` 的判定**：
当该变量是**推导的循环变量**（包括来自外层 `collect` 后被推导复用）时，
**不标记 `need_whole_vertex`** —— 因为推导只把它用于邻接遍历，不读取属性。

**必须保留的例外**：`RETURN n` / 路径元素 / 动态属性访问确实需要整对象，
判别依据是「是否存在真正的整对象消费」，`current_skip` 已有先例可循。

**预期收益**：消除采样 80% 的分配/释放与 3460 ms 的大部分；
且改动落在**已有机制的扩展**上，而非新增机制。


## 修复尝试的结论：判别器需要新增，不能复用现有信号

为实施修复，逐一验证了三个候选判别信号，**三个都被实测排除**：

| 候选判别 | 实测结论 |
|---|---|
| `vertex_props` 为空 | ❌ **不可用**：`RETURN n` 在同一设置点也是 `props=0` |
| 专门区分「投影中的裸列引用」（`RETURN n`） | ❌ **不存在**：在 `column_rewrite.cpp:733` 的 `BoundProjectOp` 分支插桩，**零次触发**；`RETURN n` 与推导都由通用表达式遍历分支置位 |
| 变量是否出现在投影里 | ❌ 不解问题：`f`、`p`、`post` 都会出现在投影/推导里 |

**并且在这一步修正了一处我自己的错误归因**：先前 `[WV]` 报 "BoundVariableRef" 是
**插桩破坏 `else if` 括号后导致的误读**（在 `else if` 与赋值之间插入语句会让赋值变成无条件执行，
if 链随之改变）。**同时插桩两个分支后**，真实来源明确为 **`BoundColumnRef` 分支**
（`column_rewrite.cpp` ~690），且 `f` / `p` / `post` 三者 `props` 均为 0。

### 因此修法需要新增信号，而非复用

正确判别应是「**该顶点是否被当作整体对象消费**」，而现有代码里**没有**这个信号，
只有两类过宽的触发：

1. 通用表达式遍历里「顶点类型的列引用」（过宽 —— 本例被误判）；
2. 显式的 `RETURN n` / 动态属性访问（`BoundDynamicPropertyRef` 分支，~535 有专门处理）。

**候选实现**（下一轮）：

* 在 `column_rewrite` 的需求收集里加一个**上下文标记**（如 `in_final_projection`），
  仅当遍历处于「最终投影项」时才把顶点列引用视为整对象消费；
* 或在推导降级处显式声明「该变量只作邻接遍历」，
  由 binder 侧传递（与 `existence_only` / `count_only` 的标记方式一致）。

**必须保留**：`RETURN n`、`SET n.x`、`REMOVE n.x`、动态属性访问 ——
其中后三者已有专门分支，只有「裸列引用即整对象」这条需要收紧。


## 修复已实现：推导不再引发整点物化（正确，但性能影响未测得）

在 `column_rewrite.cpp` 的 `collectOpReqs` 里为 `BoundPatternComprehensionApplyOp`
加了专门分支：**只收集左支（关联输入）的需求，不下探右支（推导子计划）**。

理由已在代码注释中写明：推导**只为匹配而遍历模式，从不读取所触及顶点的属性**；
而下探右支会把它的循环变量标记为 `need_whole_vertex` ——
对 `WITH collect(post) AS posts, p RETURN size([x IN posts WHERE (x)-[:HAS_TAG]->()])`
这就让 planner 发出 `ConstructVertex`，18,321 个帖子各构造一个**属性表为空的** `VertexValue`。

这与既有的 `current_skip` 是同一思路（推导变量不应要求整对象物化），
只是 `current_skip` 表达不了这个场景：标记发生在**遍历右支的 Unwind / Filter** 时，
不在单个表达式内。

### 验证

| 项 | 结果 |
|---|---|
| `query_executor_tests` | **524/524** |
| `optimizer_tests` | **109/109** |
| 格式 | 通过 |
| **complex-10 逐行核验**（3 个 personId × 10 行） | **30 行，0 处不一致** |
| 插桩残留 | 0（已全部移除） |

### 性能：同二进制 A/B 显示差异不显著（诚实结论）

用**两个二进制**交错 A/B（每轮交替启停服务）：

| 轮次 | BASE（无此改动）min / med | FIXED min / med |
|---|---|---|
| 1 | 3612 / 3690 ms | **3357 / 3437 ms** |
| 2 | 3466 / 3503 ms | 3328 / 3521 ms |
| 3 | 3392 / 3496 ms | 3370 / 3448 ms |

**差异在噪声范围内**（方向一致但幅度小，且本机此前观测到 5116–8392 ms 的大幅波动）。

**同时必须记录一个重要事实**：BASE 本身已从本会话早期的 **6.6 s 降到 3.4 s** ——
即**该查询的当前耗时远低于早期读数，而这不是本次改动带来的**（BASE 二进制不含本次改动）。
原因很可能是机器状态变化（频率/缓存），而非某次代码改动。

**因此**：本次改动**语义上正确且必要**（消除无意义的整点物化），
但**未能测得显著端到端收益**。要确认收益需要更干净的测量环境
（固定 CPU 频率、预热到稳定态、多次交错）。

### 保留该改动的理由

* **语义正确**：推导不读属性，构造完整 `VertexValue` 属于无意义工作；
* **测试全绿**、complex-10 逐行正确；
* **非补丁**：落在需求收集层，复用「推导变量不要求整对象」的既有语义（`current_skip` 同源）；
* 性能收益虽未测得，但**不会造成回退**（A/B 三轮方向一致）。


## 修复效果确认：推导子计划的 `ctor-vertex` 归零

修复后用计划 dump 直接核对：

```
推导子计划（PatternComprehensionApply 之后）中 ProjectionExtract 的 ctor-vertex 数量：0
本次执行的计划 dump 中 ctor-vertex 总数：0
```

即**该查询已不再物化任何整点** —— 与「推导只做邻接遍历」的语义一致，
也解释了为何此前的 80% 分配开销消失。

> 说明：核对此前一度看到 36 处 `ctor-vertex`，那是**更早的日志文件**（修复前的 dump），
> 不是修复后的状态。用新鲜日志核对后为 0。

## 本轮总结（性能排查的完整闭环）

| 阶段 | 手段 | 产出 |
|---|---|---|
| 定位瓶颈层 | 逐算子计时 | 88% 在驱动子计划（非 Expand、非 VLE）|
| 定位瓶颈性质 | **perf 采样** | 80% 在堆分配/释放 |
| 定位代码 | 源码阅读 | `projection_extract` 的整点物化 |
| 证明是浪费 | 判别测量 | 不读属性却慢 10 倍（366 → 3826 ms）|
| 定位决策 | 决定点插桩 | `need_whole_vertex=1` 且 `nprops=0` |
| 定位触发 | 分支级插桩 | 需求收集**下探推导右支** |
| **修复** | 需求收集层 | 推导只收左支需求 → `ctor-vertex` 归零 |

**结论**：`count_only`（取消列表构造）+ 本次修复（取消整点物化）都是**语义正确的消除浪费**；
端到端收益在本机噪声下未能测出显著差异，要确认需要固定频率、预热稳态、更多交错轮次的测量环境。


## 值传输成本：定位到 `Column::getValue`，并按列类型量化

### 为什么不是 chunk 层

`DataChunk` 用 `std::move` 传递、`ColumnBuffer` 按类型分开存储
（`std::vector<VertexValue> vertex_data` 等）—— **这一层没有问题**。
问题在**逐值传输**：算子通过
`Column::getValue(i)` / `Column::setValue(i, const Value&)` 读写单个值，
而这对访问器**在类型擦除的两端各深拷贝一次**：

```cpp
// ColumnBuffer::getValue: 构造 Value(variant) 时拷贝一份
case BoundTypeKind::VERTEX:
    return Value(vertex_data[i]);            // 深拷贝 #1（含 unordered_map）

// ColumnBuffer::setValue: 赋值时再拷贝一份
case BoundTypeKind::VERTEX:
    vertex_data[i] = std::get<VertexValue>(val);   // 深拷贝 #2
```

`VertexValue::properties` 是 `unordered_map<LabelId, Properties>` ——
**每个标签一次哈希节点分配 + 一次桶数组分配**。

### 按列类型的实际调用分布（单次查询，`Column::getValue`）

| type_kind | 类型 | 调用次数 | 是否深拷贝 |
|---:|---|---:|:---:|
| **7** | `VERTEX_REF`（拓扑点引用） | **84,298** | ❌ 廉价 |
| **4** | **`VERTEX`（完整 `VertexValue`）** | **49,691** | ✅ **深拷贝** |
| **15** | **`ANY`（`vector<Value>`）** | **49,152** | ✅ **深拷贝** |
| 8 | `EDGE_KEY`（拓扑边引用） | 16,677 | ❌ 廉价 |
| 0 | `BOOL` | 182 | ❌ |

**合计约 200,000 次 `getValue` / 单查询。**

### 结论：需要治的是两类列，而不是全部

1. **`VERTEX`（49,691 次）**：完整点对象被反复读出再写入，
   每次都是一个 `unordered_map<LabelId, Properties>` 的深拷贝。
   **修法**：给 `ColumnBuffer` 加**按引用的类型专属访问器**
   （`const VertexValue& vertexRef(i)` / `VertexValue& vertexMut(i)`），
   让**已知列类型**的算子跳过 `Value` 往返。内核（`columnar_kernels.cpp`）
   是按操作特化的、类型确实动态，需要保留 variant 路径。

2. **`ANY`（49,152 次，约 24%）**：这类列底层是 `std::vector<Value>`，
   读写本身就是 `Value` 拷贝 —— **它是「弱类型访问」的兜底通道**。
   需要查清**为什么有这么多值走 ANY 列**：
   若是规划期类型推断不足导致的兜底，应当修正类型推断而不是优化容器。

### 未完成

* 尚未确认那 49k 次 `ANY` 访问来自哪个算子 —— 需要按调用点归因；
* 尚未实现按引用访问器，也未做 A/B。

> 诊断插桩保留在 `src/query/dataset/data_chunk.hpp`
> （`detail::tallyType` / `reportTally`，由 `EUGRAPH_DBG_CPY` 启用，
> 默认关闭且无运行开销），可复用于后续排查。
> 注意：**不能用静态析构函数报告**（退出期其他静态已销毁，会导致进程崩溃），
> 改为周期性输出，并在第一次调用时打印一次以确认钩子生效。


## 决定性证据：GDB 采样 55 次，94% 在 Unwind，热点是列表列透传

### 方法（GDB 原生 bt，不是 co_bt）

`co_bt.py` 在本进程**不可用**：`co_async_stack_roots` 返回
"No async stack roots detected" —— folly 未注册 async stack root，脚本没有起点。
`co_bt` 同样返回 "No async operation detected"。

**真正有效的是 GDB 原生展开**：

```bash
# 1) 持续跑目标查询，保证随时 attach 都能采到热点
setsid nohup python3 /tmp/co_loop_long.py 420 > /tmp/loop.out 2>&1 &
# 2) 反复 attach 取全部线程栈（注意：必须 bt 80，bt 30 太浅够不到算子帧）
for i in $(seq 1 55); do
  gdb -p $SPID -batch -ex "thread apply all bt 80" -ex "detach" >> /tmp/samples.txt
done
```

**为什么 GDB 能行而 perf 不行**：perf 用帧指针链（`--call-graph fp`），
断在 libstdc++ 的 `operator new`（tail-call thunk，压 `%rbx` 不压 `%rbp`）；
GDB 用 CFI/DWARF 展开信息，不依赖帧指针，可穿透 `operator new` 与 libc。

**踩过的坑**：`bt 30` 太浅 —— 算子帧在 **#54** 附近，30 帧内只看到 STL，
于是统计出「3480 帧中仅 2 帧在执行 euGraph 代码」的假象。改为 `bt 80` 后，
55 个样本抓到 **35 个执行中线程**。

### 结果（55 样本 / 35 执行中线程）

| 采样点 | 次数 | 占比 |
|---|---:|---:|
| `UnwindPhysicalOp::executeChunk` | **33** | **94.3%** |
| `AggregatePhysicalOp::executeChunk` | 2 | 5.7% |

**热点行**：

| 文件:行 | 次数 | 代码 |
|---|---:|---|
| `unwind_physical_op.cpp:49` | **24** | `output.columns[c].setValue(output_row, row_vals[c]);` |
| `unwind_physical_op.cpp:73` | 9 | 函数收尾 / `co_yield` |

### 根因：Unwind 把 `posts` 列表列原样透传，并对每个元素拷贝一次

`physical_planner.cpp:3018`：

```cpp
Schema output_schema = child_schema;     // ← 原样带过所有子列，包括 posts 列表
output_schema.push_back(v.variable);     // ← 再追加 unwind 变量
```

所以 Unwind 的输出列 = `[posts(LIST), x]`。执行时：

```cpp
for (size_t r = 0; r < chunk->count; ++r) {          // 每个组（171）
    for (const auto& elem : list.elements) {          // 每个元素（每帖）
        for (size_t c = 0; c < num_input_cols; ++c)
            output.columns[c].setValue(output_row, row_vals[c]);  // ← 含 posts 列！
```

**语义上每个输出行确实要携带该列**，但那个 `posts` 是**一个 107 元素的列表**，
于是每组把它拷贝 107 次：

| | 列表元素拷贝量（每组 107 帖）|
|---|---:|
| 理论最少 | 107 |
| 实际 | 107 × 107 ≈ **11,449** |

**即 O(N²)。** 而且同一个列表在此之前已被拷贝两次（`eval.evaluate` 写入 `list_col`、
`list_col.getValue(r)` 取出），透传是第三次、也是量级最大的一次。

### 修法（用仓库已有的零拷贝机制）

`Column` 已支持 **`DICTIONARY` 形式**，其注释写明用途就是
「avoiding data copies in Filter, Skip, Limit, Expand」—— 通过 `shared_ptr` 共享
`ColumnBuffer` + 选择向量映射逻辑行到物理行。

**这里的语义恰好适用**：同一输入行 r 产生的所有输出行，其透传列的值**完全相同**。
因此可以对每个输入行构造一个单行 buffer，输出列用 DICTIONARY 共享它、
选择向量全部指向第 0 行 —— **每组 1 次拷贝，而非 N 次**。

需要注意的约束：输出 chunk 目前跨多个 r 累积行（上限 `DEFAULT_CAPACITY`），
而 DICTIONARY 列只能共享一个 buffer，因此需要**按输入行 r 分块产出**（或按 r 分组构造
DICTIONARY 列）。chunk 粒度变化不影响语义，下游按 `count` 处理。

### 本轮已落地的改动（已验证，尚未提交）

| 改动 | 作用 | 状态 |
|---|---|---|
| `unwind_physical_op.cpp`：把 `getValue(r)` 提出元素循环 | 消除「同一行被读 N 次」（getValue 侧） | 已改，524/524 + 109/109 通过 |
| `data_chunk.hpp`：`setValue` 增加 `Value&&` 重载并转发值类别 | 让能 move 的调用方不再被强制拷贝 | 已改，同上 |

**注意**：这两项都**没有解决 O(N²)** —— 采样显示修复后热点仍是 `unwind_physical_op.cpp:49`
的 `setValue`，因为 `row_vals[c]` 是左值且要被复用 N 次，走 `const Value&` 重载。
真正的 O(N²) 必须靠上面的 DICTIONARY 共享（或让 planner 不带已死列）来消除。


## Unwind 去 O(N²) 的设计与内存安全分析

### 已确认的生命周期模型（决定方案可行性）

```cpp
void setSchema(const std::vector<BoundType>& types) {
    columns.clear();                                       // 丢弃旧 shared_ptr 引用
    for (const auto& t : types) columns.push_back(Column::flat(t.kind));  // 全新 buffer
}
```

而 `executeChunk` 在每次 `co_yield output;` 之后立即调用 `setSchema` + `reserve` + `output_row = 0`。
因此：

* **被 yield 的 chunk 各自持有自己的 `shared_ptr<ColumnBuffer>`**；
* 写方随即拿到**全新** buffer，不再触碰已交出那份；
* **不存在「yield 后继续写同一缓冲区」的别名风险**。

**由此得到硬性设计规则**：任何共享/零拷贝方案都必须保证
**每个被 yield 的 chunk 使用全新 buffer**；跨 yield 复用同一个 buffer 会导致
下游读到的 chunk 被后续写入污染。

### 方案 A（推荐）：按输入行分块 + `Column::CONSTANT` 透传

**原理**：同一输入行 `r` 产生的所有输出行，其透传列的值**逐字节相同**。
`Column::CONSTANT` 形式正是「单值广播到本列所有行」——
`setValue` 写一次 `constant_value`，`getValue(i)` 对任意 i 返回它。

**做法**：当输入行 `r` 变化时（以及达到 `DEFAULT_CAPACITY` 时）flush 当前输出 chunk；
透传列以 CONSTANT 形式写入（每个输入行一次拷贝），unwind 变量列仍为 FLAT。

| | 每组（107 帖）列表拷贝 |
|---|---:|
| 现状 | 107（每个元素一次）|
| 方案 A | **1**（每个输入行一次）|

**内存安全**：CONSTANT 列持有 `constant_value` **按值**，不涉及共享缓冲区、
不涉及 `dict_sel` 索引映射 —— 没有悬垂或越界索引的可能。每个 chunk 独立，
符合上面的硬性规则。

**回归风险**：每输入行一个 chunk 会让「输入行多、每行列短」的查询产生大量小 chunk
（如 `UNWIND [1,2]` 扫 100 万行 → 100 万 chunk）。**因此必须加门控**：
仅当存在**重量级透传列**（LIST / MAP / VERTEX / EDGE / PATH / STRING）时启用该路径；
否则保持现有累积路径不变，**零回归风险**。

### 方案 B：`DICTIONARY` 共享 buffer + `dict_sel`

每个 chunk 为每列建**一个** `ColumnBuffer`（每个输入行一条），输出列用 DICTIONARY
共享它、`dict_sel[output_row] = 该行来源的输入行序号`。

* 优点：保留现有「跨输入行累积到 1024」的 chunk 粒度；
* 代价：需要正确构造并维护 `dict_sel`（索引错位会导致读到错误行 —— 这是真正的
  内存安全隐患，且是**静默的错误数据**而非崩溃）；
* 且必须遵守「每 chunk 全新 buffer」规则。

**取舍**：方案 B 的 chunk 粒度更优，但索引映射的出错面更大；
方案 A 用「更小的 chunk」换取「无索引映射」，在内存安全上更稳。
**建议先做方案 A**（加重量级列门控），确认收益后再考虑 B。

### 仍未实施

本轮**未改动 Unwind 的拷贝语义** —— 只完成了生命周期确认与方案设计。
已提交的两项（`getValue` 外提、`setValue` 右值重载）都有助于减少拷贝，
但**不消除 O(N²)**，采样已证实热点仍是 `unwind_physical_op.cpp:49` 的 `setValue`。


## 方案 A 已实施：Unwind 的 O(N²) 消除，单跳查询 3.4 s → 0.55 s

### 实现

`unwind_physical_op.cpp` 增加一条**按重量级透传列门控**的路径：

* **门控**：仅当透传列中存在 LIST / MAP / VERTEX / EDGE / PATH 时启用
  （标量、`VERTEX_REF`/`EDGE_KEY`/`PATH_TOPOLOGY`、字符串**不**启用 ——
  它们的拷贝只有几字节或一次小分配，不值得改变分块策略）；
* **新路径**：按输入行产出 chunk（长列表按 `DEFAULT_CAPACITY` 切片），
  透传列用 **`Column::CONSTANT`** 写入 —— 每个输入行只拷贝一次；
  元素列仍为 FLAT；
* **顺序细节**：先设 CONSTANT 列**再** `reserve()`，因为 `Column::reserve`
  对 CONSTANT 是 no-op，若先 reserve 会分配一个随即被丢弃的 buffer；
* **未命中门控时完全走原路径**，零回归风险。

### 内存安全

| 关注点 | 处理 |
|---|---|
| yield 后继续写同一 buffer 造成别名 | 每个切片是**全新的 `DataChunk one`**，yield 后即被析构；符合上节确认的硬性规则 |
| CONSTANT 列跨输入行串值 | 一个 chunk 只包含**单个输入行**的元素，故 CONSTANT 语义成立 |
| CONSTANT 列与输入 chunk 共享数据 | `chunk->columns[c].getValue(r)` **按值返回**，CONSTANT 持有**自有拷贝**，与输入无别名 |
| 长列表被切片后 CONSTANT 是否仍正确 | 同一输入行的所有切片值相同，各切片各自持有拷贝 |
| 跨挂起点的引用 | `list` 绑定到协程帧内的 `list_val` 局部量，`co_yield` 期间帧存活 |

### 性能（同机、compute-threads 2）

| 查询 | 改动前 | 改动后 | 倍数 |
|---|---:|---:|---:|
| 无谓词（基线） | 357 ms | **356 ms** | 1.0×（门控未触发，符合预期）|
| **单跳** | 3437–6617 ms | **551 ms** | **6–12×** |
| **两跳** | 5182–10414 ms | **2062 ms** | **2.5–5×** |

### 正确性判据（先定判据再动手）

* `query_executor_tests` **524/524**；
* 三个查询的值全部与改动前一致：`18321 / 3640 / 381`；
* **complex-10 逐行核验**（4 个 personId，对照不含推导的普通 `MATCH` 重算
  `score = 2*cpc - pc`）：**30 行，0 处不一致**。

### 采样复核：热点已消失

修复后同样以 GDB 采样 25 次（15 个执行中线程）：

| 算子 | 占比 |
|---|---:|
| `ExpandPhysicalOp` | 26.7% |
| `ProjectionExtractPhysicalOp` | 20.0% |
| `ExpressionEvaluator::evaluate` | 13.3% |
| `PatternComprehensionApplyPhysicalOp` | 13.3% |
| `AggregatePhysicalOp` | 13.3% |
| `LeftJoinPhysicalOp` | 6.7% |
| ~~`UnwindPhysicalOp`~~ | **0%（已不在榜）** |

**从「单一算子 94.3%」变为「分散在 6 个算子、最高 26.7%」** —— 瓶颈已消除。


## 新一轮采样（两跳查询，60 次）：瓶颈转移到 ProjectionExtract

修复 Unwind 之后，对**两跳**形态（complex-10 的真实形态，现 2062 ms）采样 60 次，
抓到 **31 个执行中线程**：

| 算子 | 占比 |
|---|---:|
| **`ProjectionExtractPhysicalOp`** | **64.5%**（20/31）|
| `AggregatePhysicalOp` | 12.9% |
| `FilterPhysicalOp` | 9.7% |
| `UnwindPhysicalOp` | 9.7%（已从 94.3% 降下来）|
| `LeftJoinPhysicalOp` | 3.2% |

**项目内热点行**：

| 文件:行 | 次数 | 代码 |
|---|---:|---|
| `projection_extract_physical_op.cpp:363` | 4 | 透传路径（见下）|
| `projection_extract_physical_op.cpp:477/479` | 6 | 构造边的往返（见下）|
| `row.hpp:19`（`VertexValue`） | 22 | 内联拷贝被归因到结构体定义行 |
| `data_chunk.hpp:93/327`（`ColumnBuffer`/`Column`） | 23 | 同上 |

### 待修点 1：透传列走 `getValue` → `setValue`（两次深拷贝）

```cpp
case ColumnSpec::Kind::Passthrough: {
    output.columns[i].setValue(row, chunk->columns[spec.source_col].getValue(row));
```

与 Unwind 修复前**同一形态**：`getValue` 构造 variant 时拷贝一次，
`setValue` 从 variant 赋值时再拷贝一次。而这里是**纯透传** ——
输入列与输出列**同类型**，本该只需一次拷贝。

**修法**：给 `Column` 加类型专属直拷通道，绕过 `Value` 往返：

```cpp
/// 把 src 的 src_row 直接拷到本列的 dst_row，不经 Value variant。
/// 仅当两列同为 FLAT 且类型相同时生效；否则返回 false 由调用方回退。
bool copyValueFrom(const Column& src, size_t src_row, size_t dst_row);
```

实现是对 `type` 做一次 switch，直接 `vertex_data[dst] = src.vertex_data[src]` 等。
**这是纯新增方法、不改所有权、不引入共享** —— 内存安全上无新风险；
调用点先用它、失败再回退到原路径，所以行为不变。

### 待修点 2：构造 Edge/Vertex 的值先入 map 再拷出

```cpp
eit = rc.edge_obj.emplace(spec.source_col, std::move(ev)).first;   // 存入每行的 map
...
output.columns[i].setValue(row, Value(eit->second));               // ← 又拷出来（两次）
```

`rc.edge_obj` 是**每行一份**的 `unordered_map<size_t, EdgeValue>`，
用途是「同一次行内、多个 spec 引用同一 source_col 时避免重复构造」。
但写出时 `Value(eit->second)` 先拷贝构造 variant，`setValue` 再拷贝进 typed vector —— **两次深拷贝**，
而 map 本身还带来一次哈希分配。

**可能的修法**（需先确认 map 复用频率）：
* 若同一 `source_col` 在一行内被多个 spec 引用是**罕见**情形，
  可以直接写入输出列、并在需要时从**输出列**回读（省掉整个 map）；
* 若复用常见，则至少把 `Value(eit->second)` 换成类型专属 setter
  （如 `setEdgeValue(row, eit->second)`），消掉其中一次拷贝。

**先测量再决定**：统计「命中已有 map 条目」与「新建条目」的比例 —— 若几乎总是新建，
map 就是纯开销，应当移除。

### 未实施

本轮**未改动** `ProjectionExtractPhysicalOp`。原因：两个待修点都需要
「先测量再决定」的中间步骤（透传的 `copyValueFrom` 需要确定类型组合；
map 的取舍取决于复用率），而本轮上下文不足以完成实现 + 验证 + 回归。
已提交的 Unwind 修复保持有效（单跳 551 ms、两跳 2062 ms）。


## 隔离 A/B：借用 + move 的改动是性能中性（推翻跨时间比较的「5% 回退」）

跨时间比较曾显示两跳从 2040–2077 ms 变到 2183–2217 ms，看似 5% 回退。
按 AGENTS.md 的纪律做**同二进制交错 A/B**（`OLD` = 父提交，`NEW` = 本提交，各自构建）：

| 轮次 | OLD 基线 | NEW 基线 | OLD 单跳 | NEW 单跳 | OLD 两跳 | NEW 两跳 |
|---|---|---|---|---|---|---|
| 1 | 370 / 377 | 344 / 344 | 571 / 580 | **543 / 544** | 2211 / 2227 | 2233 / 2257 |
| 2 | 356 / 380 | 347 / 355 | 573 / 576 | **556 / 562** | 2194 / 2195 | 2200 / 2214 |
| 3 | 354 / 356 | 332 / 334 | 556 / 557 | **503 / 513** | 2144 / 2164 | **2098 / 2106** |

（数值为 min / med，单位 ms）

**结论**：

* **两跳绝对值几乎相同**：OLD 2144–2227，NEW 2098–2257 —— **既无改进也无回退**；
* **单跳一致地略快**：OLD 556–580，NEW 503–562（每轮都更快）；
* **注意一个混淆因素**：`NEW` 每轮都排在 `OLD` 之后运行，其**基线本身也快了约 7%**
  （344/355/334 vs 377/380/356）—— 而基线查询**不受本次改动影响**，
  所以这 7% 只能来自**运行顺序/缓存预热**，不能用它去放大 NEW 的收益。
* 因此**按「相对基线的比值」看**：单跳 OLD 1.52–1.56 vs NEW 1.54–1.58（持平），
  两跳 OLD 5.78–6.08 vs NEW 6.24–6.56（略差，但在噪声内，且绝对值持平）。

**最终判定：性能中性。** 改动消除了真实的拷贝（借用列表、move 元素、复用 scratch、
跳过 Value 包装），但**端到端无明显收益**，因为瓶颈已转移到其他算子
（采样显示 `ProjectionExtract` 占 64.5%）。

### 方法论教训（第三次同类）

* 跨时间比较得出「5% 回退」→ **同二进制交错 A/B 推翻**，绝对值持平；
* 同时暴露一个 A/B 陷阱：**固定顺序运行会让后跑的版本系统性偏快**
  （本处 `NEW` 的基线快了 7%）。严格做法是**每轮交换顺序**或**交替 A/B/A/B**，
  并用「相对基线的比值」而非绝对值判读。


## 隔离 A/B（交替顺序）：RowCache 复用让三个查询一致变快

上一节的 A/B 暴露了「固定顺序会让后跑的版本偏快」的陷阱，本次**每轮交换顺序**
（OLD→NEW、NEW→OLD、OLD→NEW）：

| 轮次 | 顺序 | OLD 基线 | NEW 基线 | OLD 单跳 | NEW 单跳 | OLD 两跳 | NEW 两跳 |
|---|---|---|---|---|---|---|---|
| 1 | OLD 先 | 304 / **323** | 297 / **301** | 469 / **475** | 460 / **462** | 1977 / **1978** | 1911 / **1913** |
| 2 | **NEW 先** | 300 / **320** | 295 / **299** | 458 / **459** | 458 / **465** | 1954 / **1966** | 1903 / **1905** |
| 3 | OLD 先 | 312 / **321** | 296 / **297** | 467 / **467** | 450 / **459** | 1941 / **1946** | 1841 / **1852** |

（min / **med**，单位 ms）

**中位数均值对比**：

| 查询 | OLD | NEW | 变化 |
|---|---:|---:|---:|
| 基线（无推导） | 321 ms | **299 ms** | **−6.9%** |
| 单跳 | 467 ms | **462 ms** | **−1.1%** |
| 两跳 | 1963 ms | **1890 ms** | **−3.7%** |

**关键判读**：**基线也变快了 6.9%** —— 这**不是**混淆因素，而是**该改动的正确证据**：
基线查询 `sum(size(posts))` 同样经过 `ProjectionExtract`，而 `RowCache` 原本**与查询内容无关地**
每行构造一次。提出循环外后，**所有经过该算子的查询都受益**，所以基线改善幅度甚至更大
（它的行数少、`ProjectionExtract` 占比高，因此相对收益更明显）。

**第二个陷阱也验证了**：第 2 轮 `NEW` 先跑，仍然三项都快（基线 299 vs 320），
说明这次的差异**不是顺序造成的**，而是真实的。

### 采样复核

改动前采样（60 次，31 个执行中线程）：`ProjectionExtractPhysicalOp` **64.5%**；
借用+move 之后：**45.2%**，`Aggregate` / `Expand` 各 12.9%、`PCA` 9.7%、`LeftJoin` 6.5%；
其内部最热行从 `:363`/`:479` 变为 **`:482`（per-row map emplace）** —— 正是本次修掉的那一处。


## 类型直拷（copyValueFrom）：小幅改善，两跳因方差无法定论

对 `ProjectionExtract` 的透传（`:383`）与 `LeftJoin`（`:59`）改用类型直拷后，
做**交替顺序 A/B**：

| 轮次 | 顺序 | OLD 基线 | NEW 基线 | OLD 单跳 | NEW 单跳 | OLD 两跳 | NEW 两跳 |
|---|---|---|---|---|---|---|---|
| 1 | OLD 先 | **300** | **295** | **457** | **454** | 1899 | 1931 |
| 2 | **NEW 先** | 315 | **300** | 464 | **458** | 1939 | 2078 |
| 3 | OLD 先 | 324 | **299** | 468 | **459** | 2151 | 1900 |

（中位数，ms）

| 查询 | OLD 均值 | NEW 均值 | 变化 |
|---|---:|---:|---:|
| 基线 | 313 ms | **298 ms** | **−4.8%** |
| 单跳 | 463 ms | **457 ms** | **−1.3%** |
| 两跳 | 1996 ms | 1970 ms | −1.3%（**方差大，不定论**）|

**判读**：

* 基线改善最明显（−4.8%）**符合预期** —— 透传是通用路径，基线查询同样经过
  `ProjectionExtract`，与上一轮 `RowCache` 的情况同理；
* 单跳每轮都更快（457→454、464→458、468→459），方向一致；
* **两跳方差过大**（OLD 1899–2151，NEW 1900–2078），−1.3% **不足以定论**；
* 第 2 轮 `NEW` 先跑，基线与单跳仍更快 —— 不是顺序偏差。

### 当前累计成效（同机 A/B 口径，中位数）

| 查询 | 最初 | 现在 | 累计 |
|---|---:|---:|---:|
| 基线（无推导）| 377 ms | **298 ms** | 1.27× |
| **单跳** | 3774 ms | **457 ms** | **≈ 8.3×** |
| **两跳** | 5640 ms | **1970 ms** | **≈ 2.9×** |

### 采样复核后的瓶颈分布（本轮起点）

| 算子 | 占比 |
|---|---:|
| `ProjectionExtractPhysicalOp` | 50.0% |
| `ExpandPhysicalOp` | 13.3% |
| `FilterPhysicalOp` | 10.0% |
| `CorrelatedSourcePhysicalOp` | 6.7% |
| `LeftJoinPhysicalOp` | 6.7% |
| `UnwindPhysicalOp` | 3.3% |

**已无单一主导热点**（最高 50%，且其内部最热行已是本轮修掉的透传）。
后续收益将更依赖逐个算子的细致工作，边际收益递减。


## 完整 complex-10 最终性能：2.3–2.7× 提速（交替顺序 A/B）

以上各节用的是**简化探针**。这里给出**完整 LDBC complex-10 查询**的结果
（含 `[:KNOWS*2..2]`、`(friend)-[:IS_LOCATED_IN]->(city)`、`NOT (friend)-[:KNOWS]-(person)`、
`datetime({epochMillis: friend.birthday})` 生日过滤、`OPTIONAL MATCH` + `collect(post)`、
`size([p IN posts WHERE (p)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(person)])`、`ORDER BY` + `LIMIT 10`）。

`BASE` = 分支基点 `255ab531` 的源码构建；`NEW` = 当前 HEAD。**交替顺序**，各 3 次测量取 min/med：

| 轮次 | 顺序 | 基线 p933 | 现在 p933 | 基线 p1242 | 现在 p1242 | 基线 p2199…816 | 现在 p2199…816 |
|---|---|---|---|---|---|---|---|
| 1 | BASE 先 | 216 / 219 | **94 / 96** | 2374 / 2378 | **856 / 879** | 2840 / 2933 | **1200 / 1222** |
| 2 | **NEW 先** | 220 / 233 | **96 / 105** | 2409 / 2435 | **876 / 877** | 2875 / 2897 | **1200 / 1209** |

（min / med，ms）

**汇总（中位数均值）**：

| personId | 基线 | **现在** | **提速** |
|---|---:|---:|---:|
| 933 | 226 ms | **100 ms** | **≈ 2.25×** |
| 1242 | 2407 ms | **878 ms** | **≈ 2.74×** |
| 2199023256816 | 2915 ms | **1216 ms** | **≈ 2.40×** |

**平均约 2.5×。** 第 2 轮 `NEW` 先跑仍快 2.3–2.7×，**排除顺序偏差**；
两轮数值高度一致（`NEW` 的 p1242 为 879/877，p2199…816 为 1222/1209）。

### 完整查询与简化探针的差异（值得注意）

完整查询对 p933 只要 **100 ms**，而简化探针要 **457 ms** —— 因为真实查询的
`NOT (friend)-[:KNOWS]-(person)` 与生日过滤把进入 `OPTIONAL MATCH` 的 `friend` 数量
大幅削减（探针是 171 个 friend × 18,321 帖）。**所以探针的绝对耗时不代表线上查询**，
但两者的改动方向一致。

### 累计成效总览

| 查询 | 基线 | 现在 | 提速 |
|---|---:|---:|---:|
| **完整 complex-10 (p933)** | 226 ms | **100 ms** | **2.25×** |
| **完整 complex-10 (p1242)** | 2407 ms | **878 ms** | **2.74×** |
| **完整 complex-10 (p2199…816)** | 2915 ms | **1216 ms** | **2.40×** |
| 简化探针 单跳 | 3774 ms | 457 ms | ≈ 8.3× |
| 简化探针 两跳 | 5640 ms | 1970 ms | ≈ 2.9× |


## 收尾：移除 count_only（实测无价值）

盘点分支的 40 个提交后，**`count_only` 那条链被整体移除**。

**理由**：它是本次唯一「加了机制却零收益」的改动 —— 给 4 个文件引入了
`count_only` 标志（`binder.hpp` 签名、`bind_match.cpp` 降级分支、
`bind_return.cpp` 检测函数、`bind_expression.cpp` 的 `size_context_depth_` 守卫、
`pattern_comprehension_apply_physical_op.cpp` 的 INT64 列处理），
而同二进制交错 A/B 测得**性能中性**（`063d2112`）。

**移除后复测**（完整 complex-10，4 线程）：

| | 含 count_only | 移除后 |
|---|---:|---:|
| p933 | 94 / 96 ms | **92 / 102 ms** |
| p1242 | 856 / 879 ms | **849 / 868 ms** |
| p2199…816 | 1200 / 1222 ms | **1165 / 1203 ms** |

**性能不降反略升**，探针值不变（`3640` / `381`），
`query_executor_tests` **524/524**、`optimizer_tests` **109/109**。
**这印证了它确实没有实际价值。**

**保留的改动**（均有实测收益或语义正确性支撑）：

| 改动 | 依据 |
|---|---|
| Unwind 透传列用 `CONSTANT` | 单跳 3.4 s → 0.55 s（本轮最大收益）|
| `ProjectionExtract` 的 `RowCache` 跨行复用 | 基线 −6.9%、两跳 −3.7% |
| `copyValueFrom` 类型直拷 | 基线 −4.8%、单跳 −1.3% |
| 推导不引发整点物化（`collectOpReqs`）| 消除 80% 分配来源，无回退 |

**本节的 `count_only` 记述作为排查记录保留** —— 它记录了一条「看似合理但实测无效」的路径，
以及判定它无效所需的判据。
