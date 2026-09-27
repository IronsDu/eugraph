# 已知缺陷待办

> 排查 complex-10 正确性缺陷过程中**顺带确认**、但与 complex-10 **无关**的既有缺陷。
> 每条都附最小复现或代码位置，可直接开工。
>
> 来源复盘见 [comprehension-defect-debugging-notes.md](comprehension-defect-debugging-notes.md)。

## 1. 反向关系模式不匹配

**现象**：同一份数据上，正向有结果、反向返回空。

```cypher
MATCH (post:Post)-[:CREATED]->(p:P)   // 有结果
MATCH (p:P)<-[:CREATED]-(post:Post)   // 返回空   ← 缺陷
```

**复现**（单元测试 fixture 规模即可，见 `tests/test_query_executor.cpp` 中
`ListComprehensionPatternPredicate*` 的建图方式）：建 `post -[:CREATED]-> person` 一条边，
两种写法查询同一对节点。

**影响**：所有以 `<-[:R]-` 书写的查询都可能漏结果，属**正确性**问题，优先级高。
本次排查的 fixture 全部改写成正向以绕开它。

**定位起点**：`ExpandPhysicalOp` 的 `direction` 处理与 `Direction::IN` 的扫描方向；
`bindExpand` 中 `LEFT_TO_RIGHT` / `RIGHT_TO_LEFT` 的语义与 `physical_planner` 的 `scan_dir` 换算。

## 2. 关联变量注册类型硬编码 `Vertex()`

**位置**：`src/query/planner/binder/bind_match.cpp`，`bindExistsSubPlan` 注册
`extra_corr_vars` 处（约 1314 行）。

```cpp
ci.name = var_name;
ci.type = BoundType::Vertex();     // 硬编码：对 LIST / EDGE 等类型不成立
```

**影响**：当被关联的变量不是顶点（例如外层列表推导的列表、边）时类型错误，
下游 `Unwind` 等算子按错误类型读取。实测：`posts`（LIST）被注册成 VERTEX。

**修法**：类型取自 `saved_ctx.symbols` 中该变量的真实类型，回退到 `Vertex()`。

## 3. 按 slot 反查变量名存在歧义

**位置**：`bind_match.cpp` 构建 `BoundCorrelatedSourceOp` 的循环（约 1309 行）。

```cpp
for (const auto& [outer_slot, sub_slot] : correlation) {
    for (const auto& [name, info] : ctx_.symbols) {   // unordered_map，顺序不确定
        if (sub_slot == info.slot_id) { source.variables.push_back(name); break; }
    }
}
```

**问题**：一个 slot 上可能坐着**多个名字**（如 `__exists_saved_N` 与其保存的原变量），
反查命中哪个取决于哈希顺序。

**修法**：让关联条目**显式携带变量名**（`Correlation` 增加 `right_var`），彻底不做反查。
这与 AGENTS.md 的 `VariableId = SlotId` 红线一致：手上有 slot 就不该回头按名字查。

## 4. Apply 与 SemiJoin 对同一 `CorrelatedSource` 的列集合需求冲突

**现象**：`PatternComprehensionApply` 需要 source 暴露 `[循环变量, 列表]`；
`SemiJoin`（嵌套 EXISTS）需要 source 暴露 `[friend, __exists_saved_N]`。
二者**共用同一个 `BoundCorrelatedSourceOp` 构造**（`bind_match.cpp:1309`），
所以任何单一列集合都无法同时满足。

**实测**：把 `__exists_*` 从 `source.variables` 过滤掉 → `PatternPredicateTwoNodes`、
`NotBarePatternExpressionInReturn`、`NestedExistsWithCorrelatedPropertyFilter` 三个测试失败。

**修法方向**：不改列集合，而是让 `PatternComprehensionApplyPhysicalOp` 按
**右子计划实际引用的 slot**（`rr.slot_layout`）决定注入集合，
`correlation` 仅作为「slot → 外层列」的查找表。

## 5. CSV loader：同名关系类型多文件时列索引越界

**现象**：导入 LDBC sf0.1 时边加载中途 `SIGABRT`：

```
stl_vector.h:1253: vector<int>::operator[]: Assertion '__n < this->size()' failed
```

**根因**：`buildEdgeTypeSchemas` 以 **edge type** 为键复用 schema，同类型的多个文件共享
`schema.properties`（各文件属性的**并集**），而 `loadOneEdgeFile` 的 `prop_cols` 来自**当前文件**表头，
逐行循环用一方索引另一方。命中 LDBC 映射里被声明多次的 5 个类型：
`HAS_CREATOR`、`HAS_TAG`、`IS_LOCATED_IN`、`REPLY_OF`、`LIKES`。

**已修**：commit `bafeaaae`（PR #203 已合并）—— 属性改为按**列名**在当前文件内解析。

**遗留**：该修法只解决越界，未验证「同名多文件属性并集」在**属性类型不一致**时是否仍正确。

## 6. 测量陷阱：CPU 频率会降到 ~33%

**现象**：本机（AMD Ryzen 7 5825U）`lscpu` 的 `CPU(s) scaling MHz` 会显示 33–34%，
此时**同一二进制**的 complex-7 比全频时慢约 2 倍（pid 933：min 8.4ms → 15.8ms）。

**教训**：跨时间比较性能前必须确认当前频率，否则会把功耗状态误判成代码回归
（本次排查中就因此误判过一次）。性能结论一律用**同二进制交错 A/B**。

## 7. 客户端长连接会被断开 —— 已修复（2026-09-26）

**现象**：单个 bolt 连接连续执行若干次后，客户端报 `Failed to read from defunct connection`；
**服务器未崩溃**（仍在监听、日志正常、内存充足）。服务端日志为
`[bolt] read error: AsyncSocketException: ReadCallback::getReadBuffer() returned empty buffer`。

**根因**：`BoltConnection::getReadBuffer()` 只在读缓冲为空时重置，但 `processMessage()`
消费完一条消息就返回，**半条消息的残留字节**让 `length()` 极少归零；而 `trimStart()`
只推进数据指针、不回收前面的空间。偏移于是前进到 64 KiB 末尾，`tailroom()` 归零，
folly 抛 `returned empty buffer` 并断连。耗尽时间只取决于**每次执行的字节数**：
实测 `RETURN 1`（约 78 B/次）第 ~840 次断、`RETURN '<1.5 KB>'`（约 1560 B/次）第 ~41 次断
（≈ 64 KiB ÷ 每次字节数）。

**修法**：`getReadBuffer()` 在 `tailroom()` 低于阈值时**回卷**缓冲区（把未消费残留搬进
新分配缓冲区）。设计与实测见 [service/neo4j-bolt-protocol.md §11](../service/neo4j-bolt-protocol.md)。

**影响（修复前）**：基准脚本需每轮新建连接，否则会把「连接断开」误判为「服务器崩溃 / 查询超时」；
官方 LDBC driver 不会为每条语句重连，因此跑到 60–100 次操作时**整个 run 被
`ServiceUnavailableException` 打断**——这是"跑完整官方 benchmark"的前置缺陷。

**验证**：`TestConnectionLifetime`（4 个用例）在修复前的二进制上失败、修复后通过；
`scripts/repro_bolt_connection.py`（直接压单/多连接，基准脚本做不到）4 并发 × 300 次全过；
官方 driver（4 线程 / 200 操作）完成且服务端零 read error。

## 8. `CREATE INDEX` 回填阶段 SIGSEGV，并留下空索引

**现象**：在 135k–287k 行的大标签上执行 `CREATE INDEX` / `CREATE UNIQUE INDEX`，服务端日志：

```
[handler] Created vertex index 'idx_msg_cd' (id=12) on Message.(creationDate)   ← 报"成功"
*** Signal 11 (SIGSEGV) received by PID 56168 (pthread TID ...) (code: address not mapped to object), stack trace: ***
```

即 **catalog 元数据先提交、回填时崩溃**，随后服务端无响应。

**后果链**（本轮实测）：
1. catalog 里留下**定义为空**的索引：`CALL db.indexes()` 能列出该索引，但它没有条目；
2. 本应走索引的点查退化为全标签扫描：
   `MATCH (m:Message {id: 3})` **946 ms**（索引完好时 4 ms）、`Post{id}` 461 ms、`Comment{id}` 495 ms；
   而小标签（≤16k 行）回填能在超时内完成，点查仍是 1 ms —— 于是故障表现为**只在大表上慢**；
3. LDBC 的短查询（short-4/5/6/7）全部是 `Message{id}` 点查，因此在坏实例上比 neo4j 慢 5–14×，
   在好实例上比 neo4j 快 52–180×。**索引状态不核对，性能结论就会整体反向**。

**loader 侧的放大因素**：
* `src/program/shell/rpc_client.cpp:47` 的 `channel->setTimeout(30000)` 被 loader 共用，
  大表回填超过 30 s 即 `TTransportException: Timed out`；
* `csv_loader.cpp:782` 对该失败**只 log warn 后继续**，`loader_main.cpp` 也不校验索引是否可用，
  于是"加载成功"的实例可能带着一堆空索引。

**待办**：
1. 查回填路径的 SIGSEGV 根因（崩溃在 `Created vertex index` 之后、异步栈上）；
2. catalog 与回填改成一个事务/两阶段：回填成功后才可见，避免留空索引；
3. loader 的 DDL 超时改为可配置（或对建索引用更长超时），并在加载结束做
   "索引存在且点查命中"的校验，失败即报错退出；
4. 重测 benchmark：任何 `MATCH (... {id: ...})` 类读数前，先证明目标标签的索引可用。

## 9. 属性访问：谓词侧 k×N 次独立点查（单次 ~1.3 µs），与并发膨胀是**两个独立效应**

### 9.1 谓词属性的获取流程（用计数器实测，非推断）

计划里同一顶点的多个谓词属性会被合并成**一个** `vprop-coalesce` spec，但该 spec 的**执行**
仍是按属性逐个取：

```
ProjectionExtract（LoadVertexPropCoalesce）
  for (label, prop_id) in candidates:                      // ← k 个属性，k 次
      batchGetVertexProperties(ids, label, {prop_id})
        → AsyncGraphDataStore 投影分支:
            for vid in ids:                                // ← N 个顶点
                Properties props; props.resize(max(proj)+1) // ← 每顶点一次分配
                for pid in proj:
                    store_.getVertexProperty(vid, label, pid) // ← 每个 (顶点,属性) 一次 B-tree 点查
```

临时加 `CALL db.pocAttrCounters()` 暴露的计数器（同一 JOIN 结构，117k 行）直接印证：

| 变体 | 耗时 | **点查次数** | 每行 |
|---|---:|---:|---:|
| 0 属性 | 475 ms | **0** | 0 |
| 1 属性在 WHERE | 681 ms | **239,728** | 2.05 |
| 3 属性在 WHERE | 1054 ms | **719,184** | **6.15** |
| 投影物化整顶点 | 825 ms | **0** | 0（走前缀扫描） |
| 投影 3 属性 | 817 ms | **0** | 0（走前缀扫描） |
| complex-9 原查询 | 1563 ms | **881,637** | **7.5** |

* **谓词侧**：每个属性 × 每个顶点一次点查；1 属性 239,728 次 ≈ 2.05/行（HAS_CREATOR 遍历 + 属性读）；
  3 属性 719,184 次 = **3 × 239,728**，完全线性；
* **投影侧**：0 次点查——走 `getVertexPropertiesBatch()` 的 `search_near(prefix)` 前缀扫描，
  一次取回该顶点全部属性，所以**投影侧多属性几乎免费**（817 vs 825 ms）；
* **单次点查 ≈ 1.31 µs**（(1054−681)/(719184−239728)）；
* complex-9 的 **881,637 次点查 ≈ 1.15 s CPU**，占其 1.5 s 总耗时的约 **75%**。

**尝试过的修法（已实测并回退）**：把谓词侧也改成"按 label 分组后走前缀扫描一次取全部"。
结果**更慢**：1 属性 681→862 ms、3 属性 1054→1474 ms、complex-9 1563→1792 ms。
原因：前缀扫描对每个顶点都要 `search_near` 后顺序遍历该顶点的所有属性行，
而点查只走一条索引路径——**前缀扫描并不比点查便宜**。改动已 `git checkout` 回退。

### 9.2 属性开销 **不能**解释并发膨胀（实测分离）

固定属性数测每查询 CPU（1 线程 vs 4 线程），并扣除"0 属性"的框架固定开销：

| 变体 | 1t CPU/查询 | 4t CPU/查询 | 总体膨胀 | **纯存储部分膨胀** |
|---|---:|---:|---:|---:|
| 0 属性（仅展开） | 285.1 ms | 376.3 ms | 1.32× | — |
| 1 属性在 WHERE | 543.6 ms | 1147.1 ms | 2.11× | **3.15×** |
| 3 属性在 WHERE | 1052.7 ms | 2972.9 ms | 2.82× | **3.12×** |

**结论**：扣除固定开销后，**"纯存储部分"的膨胀在 1 属性和 3 属性下几乎相同（3.15× vs 3.12×）**。
⇒ 属性数只决定"每查询要做多少工作"（1t 285→543→1053 ms 线性增长），
**并发膨胀由访问的性质决定，与属性数无关**。两者必须分开处理：

* **属性数效应**（本节 9.1）：k×N 次点查 → 可用"按顶点一次取回同一 label 的多属性"改善，
  但实测前缀扫描更慢，需另找路径（例如让 `search_near` 只取需要的少数属性、或减少遍历项）；
* **并发膨胀效应**：0 属性时仅 1.32×，**一旦有随机点查就跳到 ~3.1×**——
  指向随机访问下的**内存级并行（MLP）饱和**：并发时每个线程的随机点查无法用预取掩盖延迟，
  多核同时压内存子系统导致每单位工作更慢。顺序访问的对照支持这一点
  （全表 `count` 从 45.0→59.7 ms 仅 1.32×，而随机点查 3.1×）。

### 9.3 与 neo4j 的同类对照：他们每行 0.15 µs 且**并发零膨胀**

同机、同口径（`consume()` + `/proc` CPU 增量）三种访问形态：

| 场景 | eugraph 1t | eugraph 4t | 膨胀 | neo4j 1t | neo4j 4t | 膨胀 |
|---|---:|---:|---:|---:|---:|---:|
| 纯顺序扫描（全表 count） | 39.8 ms | 47.0 ms | **1.18×** | 0.19 ms | 0.18 ms | 0.95× |
| 顺序 ＋ 每行 1 属性 | 525.7 ms | 2034.5 ms | **3.87×** | 43.6 ms | 48.2 ms | **1.11×** |
| 随机跳转 ＋ 属性 | 576.2 ms | 1199.4 ms | 2.08× | 53.4 ms | 52.6 ms | **0.99×** |

每行成本我们 **1.8 µs** vs neo4j **0.15 µs**（约 12×）；neo4j 三种形态**全部零膨胀**。

### 9.4 分层微基准与调用形状（确定性数据，非推断）

**存储层单位成本**（`tests/tools/attr_fetch_bench.cpp`，直连 WT，Message = label 2 / 135,701 行 / 8 属性）：

| 取法 | 单位成本 |
|---|---:|
| 标签扫描（基准） | 0.066 µs/行 |
| 点查单属性 | **0.73–0.94 µs/次** |
| scan-all（一次取回该顶点全部属性） | **1.76 µs/次** |
| 批量 scan-all | 1.31 µs/次 |
| 复刻 coalesce 链条（点查 + `Properties` 分配） | 1.02 µs/行/属性 |

**属性批量调用的形状**（临时计数器 `db.pocPropBatchShape`，117k 行，同一批内比较）：

| 变体 | 耗时 | scan-all 次数/行 | 投影分支 次数/行/属性数 | **每行属性读** |
|---|---:|---:|---:|---:|
| L0 只遍历（不读属性） | 626 ms | 5 / 121,182 | — | **0.00** |
| L1 **谓词 1** 属性 | 776 ms | 5 / 121,182 | 3 / 239,728 / 3 | **2.05** |
| L3 **谓词 3** 属性 | 1243 ms | 5 / 121,182 | 9 / 719,184 / 9 | **6.15**（= 3×） |
| P1 **投影 1** 属性 | 844 ms | 9 / 480,774 | — | 0（走 scan-all） |
| P3 **投影 3** 属性 | 866 ms | 9 / 480,774 | — | 0（走 scan-all） |

**读法**：
1. **谓词侧每行 2.05 次属性读**（1 个属性），**每多一个谓词属性再 +2.05 次**（线性）；
2. **投影侧完全不进投影分支**——走 scan-all，且**属性数不改变调用量**（P1 与 P3 的
   scan-all 次数/行数**完全相同**）⇒ 这就是"投影多属性几乎免费"的直接原因；
3. 投影侧每行 ~1.86 µs ≈ 存储层 scan-all **1.76 µs** ⇒ **引擎层自身开销很小**，
   代价几乎全部落在存储调用上。

**为什么整条"取值→Value→列写入"链不是问题**（微基准，N = 50 万）：

| 动作 | 成本 |
|---|---:|
| 直接写 `int64_data[i]` | 0.004 µs/行 |
| `setValue(Value(v))`（Value 构造 + 变体分派） | 0.007 µs/行 |
| 每行一个 `vector<PropertyValue>` 中间缓存 | 0.069 µs/行 |
| 完整链条（缓存 + setValue） | 0.072 µs/行 |
| `getValue()` + `holds_alternative` + `get<>` | 0.007 µs/行 |

整链 **0.072 µs/行**，相对每行 1.8 µs **可忽略** ⇒ 优化引擎的逐行小动作没有意义。

### 9.4.1 coalesce 路径的代价结构（负结果 + 机制分析）

**改动尝试**：把 coalesce 执行的"每个候选一次 `batchGetVertexProperties`"改为"按 label 分组后
一次取回该 label 的多个属性"。同批 A/B（min / median，5 轮）：

| | L0 | L1 | L2 | L3 | P1 | P3 |
|---|---:|---:|---:|---:|---:|---:|
| 改前 | 279.5 | 527.8 | 780.3 | 1035.5 | 756.1 | 804.6 |
| 改后 | 283.8 | 523.8 | 773.2 | 1034.8 | 747.3 | 798.6 |
| 差异 | +1.5% | −0.8% | −0.9% | −0.1% | −1.2% | −0.7% |

**全部落在 ±1.5% 噪声内**（正确性检查通过：573 + 50 测试全过、计数语义一致）⇒ **已回退**。

**机制结论**：把 k 次属性读合并成 1 次**几乎没有收益**，说明成本不是"每次属性读"而是
**每次批量调用的固定开销 + 每行的循环开销**：
`batchGetVertexProperties` 每次都要跨池 dispatch、开游标、并按行循环；合并调用次数并不减少
每行的处理量（仍要构建 `ref_vids`/`ref_rows`、按 label 过滤、写缓存）。

**这也解释了 §9.4 里"谓词每行 2.05 次属性读"到底是什么**：它是**每个候选 label 各一次**
（`coalesce_candidates` 混了 `Post`/`Message`/`Comment` 的属性空间，同一逻辑属性在不同 label
下的 prop_id 不同），而**不是同一属性被重复读**。把它降到 1 次只值 1%。

**因此剩下真正的结构性开销是：为谓词属性走 `LoadVertexPropCoalesce` 这条路径本身**
（每个候选 label × 每行一次取用），而投影侧走的是"整顶点物化后复用"（`need_whole_vertex`
分支在 `column_rewrite.cpp` 里**直接跳过所有属性加载**）。要改变量级，需要让谓词属性也走
`LoadVertexProp` 单属性 spec——但谓词属性的候选天然是多 label（`Post`/`Message` 属性空间
不同），且 `evalPropertyRef` 对 `VertexRef` 目前**只处理 `n.id` 结构字段**
（`src/query/evaluator/expr/eval_property.cpp:144`），不具备按单 label 取属性的能力。
这是一个需要设计评估的改动，不是局部修补。

### 9.5 已尝试并**实测否决**的三个改动（勿重走）

| # | 改动 | 结果 |
|---|---|---|
| ① | 谓词侧改前缀扫描（一次取该顶点全部属性再切片） | **更差 16%**（681→862 ms）——`search_near` 需对每个顶点顺序遍历其属性行，1.76 µs 比点查 0.75 µs 贵 |
| ② | 投影分支单属性时省掉 `Properties` 分配 | 预期省 0.03 s，**实测在噪声内（±2%）**，部分变体反而略差 → 回退 |
| ③ | 投影分支多属性改 scan-all 切片 | 同上，无改善 → 回退 |
| ④ | coalesce 按 label 分组，减少批量调用次数 | 同批 A/B 全在 ±1.5% 噪声内（L1 −0.8%、L2 −0.9%、P1 −1.2%）→ 回退；说明成本在"每次调用固定开销 + 每行循环"，不在属性读次数 |

**已否决的方向汇总**（累计）：顶点属性合并存储（用户已否，且实测 scan-all 比点查贵）、
谓词侧批量改前缀扫描、引擎逐行链条优化、跨池跳转 POC、协程迁移、线程池扩容、锁/忙等。

**当前唯一被数据支持的优化目标**：**谓词侧的 2.05 次属性读/行/属性**。
它由规划器把谓词属性解析为 `LoadVertexPropCoalesce`（每个候选 label × 每行各一次取用）产生，
而投影侧走的是"整顶点物化后复用"的路径。**下一步应查规划器为何对谓词属性选择 coalesce 而非
`LoadVertexProp` 单属性 spec**——后者在计划里存在（`RETURN m.creationDate` 用它），
若能用于谓词，预期把 2.05 次/行降到 1 次/行甚至更低。

## 9.6 **并发下降的定位：在存储层内部，与引擎/执行器无关**（2026-09-27）

前面 9.1–9.5 把引擎、规划器、逐行取值链、批量调用形状全部实测排除后，仍未找到并发下降的
原因。本轮把并发实验**下沉到存储层**（新增 `tests/tools/concurrency_bench.cpp`，直连
`SyncGraphDataStore`，完全绕开算子、协程、线程池与调度器），原因立刻显形：

| 线程 | 共享 `defaultSession_` | | 每线程独立事务（独立 session） | |
|---:|---:|---:|---:|---:|
| | 每次查找 | 吞吐 | 每次查找 | 吞吐 |
| 1 | 1.007 µs | 0.99 M/s | **0.663 µs** | **1.51 M/s** |
| 2 | 1.418 µs | 0.71 M/s | 1.187 µs | 0.84 M/s |
| 4 | 1.678 µs | 0.60 M/s | 1.391 µs | 0.72 M/s |
| 8 | **1.734 µs** | **0.58 M/s** | **1.633 µs（+146%）** | **0.61 M/s（−60%）** |

**结论**：
1. **并发下降发生在存储层内部**——裸调用 `getVertexProperty` 在 1→8 线程下每次查找 +146%、
   吞吐 **−60%**。既然完全绕开了引擎与协程，此前对引擎层的所有怀疑（逐行取值链、批量形状、
   规划器 spec 选择、协程迁移、跨池跳转、线程池、锁、忙等）**都不是并发下降的原因**；
2. **共享 session 有额外代价**（1.007 vs 0.663 µs），但**不是主因**——独立 session 下吞吐仍
   下降 60%。注意 `getSession(INVALID_GRAPH_TXN)` 返回的正是共享的 `defaultSession_`
   （`wt_store_base.cpp:129`），故凡是走 `INVALID_GRAPH_TXN` 的路径在多线程下都会额外吃亏；
3. 剩余嫌疑集中在 WiredTiger 侧：**同一 B-tree 的页级并发**（多线程各自开游标遍历同一张表，
   页结构/父页/hazard pointer 的共享读）与**每次调用都新建游标**（`openCursor` →
   `WtCursor(session, table)`）。

**下一步（按可验证性）**：
1. 复用游标：把 `getVertexProperty` 里每次 `openCursor` 改为按 session 复用游标
   （WiredTiger 的 `session->open_cursor` 对同名游标是幂等的），看 8 线程吞吐是否回升；
2. 对照不同表：多线程分散访问**不同 label 表**（不同 B-tree），若吞吐回升则确认是
   同一 B-tree 的页级争用；
3. 若确认在 WiredTiger 页级，评估会话/事务隔离配置（如只读事务、`readonly` 配置、
   快照隔离下的读并发参数）。

## 9.7 **根因：每次存储操作都抢一把全局互斥锁**（2026-09-27）

§9.6 把并发退化定位到存储层内部；本轮进一步找到具体机制——**`WtStoreBase` 里的一把成员
互斥锁 `sessionMutex_`，在每一个存储读写原语上被无条件获取**：

```cpp
// src/storage/wt_store_base.hpp
mutable std::recursive_mutex sessionMutex_; // protects defaultSession_ from concurrent use

// src/storage/wt_store_base.cpp —— 热路径 4 个函数全部无条件加锁
bool         tablePut(...)  { std::lock_guard<std::recursive_mutex> lock(sessionMutex_); ... }  // :214
std::optional<std::string> tableGet(...) { std::lock_guard<...> lock(sessionMutex_); ... }        // :231
bool         tableDel(...)  { std::lock_guard<...> lock(sessionMutex_); ... }                     // :246
void         tableScan(...) { std::lock_guard<...> lock(sessionMutex_); ... }                     // :263
```

而 `getVertexProperty` → `tableGet`（`sync_graph_data_store.cpp:368`），所以**每一行的每一个
属性点查都要抢这把全局锁**。其余 4 处（`openConnection`/`closeConnection`/`ensureGlobalTable`/
`checkpoint`）不在热路径上，属正当用途。

**为什么生产环境必然全程争用**：`getSession(txn)` 对 `INVALID_GRAPH_TXN` 返回共享的
`defaultSession_`（`wt_store_base.cpp:129`），而**引擎与服务层根本不调用 `beginTransaction()`**
（`src/query/`、`src/service/`、`src/program/` 内零调用点）⇒ **所有读查询都用共享 session**，
这把锁因此**必然**被所有线程争抢。锁的注释说明其目的正是"保护 defaultSession_ 不被并发使用"
——WT session 确实非线程安全，**但用一把全局锁来保护，代价是把整个存储层串行化了**。

**证据链（三条独立判据，互相印证）**：
1. **吞吐随线程数下降**：裸 `getVertexProperty`（绕开引擎/协程/调度器）1 线程 0.58–0.63 M/s
   → 8 线程 **0.62 M/s，每次查找 +147%**（`tests/tools/concurrency_bench.cpp`）。
   若是并行度不足，吞吐应持平或上升；**下降**只有串行化资源能解释；
2. **与工作集大小无关**：工作集 1000 个顶点（~0.24MB，可驻留 L2）退化 **+162%**，
   50000 个（~12MB，超出 L2）退化 **+155%** —— **彻底排除缓存容量/内存带宽**（原 MLP 假说否证）；
3. **代码里存在且仅存在这一处全进程串行点**，且生产路径必然命中它。

**方法学修正（重要）**：§9.6 首版按 1→2→4→8 顺序测量，而本机 CPU 频率会漂移（AGENTS.md 记录
可达 2 倍），顺序测量会把频率漂移误判为并发退化。现已改为**多轮交错、每档取 min**，
上述数字均出自交错测量（见 `tests/tools/concurrency_bench.cpp` 头部说明）。

**修法（已实施）**：
1. **停止共享 session**：为无事务访问提供**每线程 session**（session 池 / 每线程惰性创建），
   session 归 store 所有并在 `closeConnection()` 统一关闭，避免 thread_local 析构晚于连接的
   生命周期问题；
2. **锁改为按需**：仅当传入的 session 是共享 `defaultSession_` 时才加锁；每线程/每事务私有
   session 无需全局锁（`tablePutTxn` 已存在，说明 txn 路径本就该走独立通道）；
3. **实测收益**（complex-9 核心，`consume()` 口径）：

   | 线程 | 修前 ops/s | 修前 CPU/查询 | 修后 ops/s | 修后 CPU/查询 |
   |---:|---:|---:|---:|---:|
   | 1 | 1.77 | 563 ms | 1.90 | 520 ms |
   | 2 | 2.74 | 689 ms | 3.57 | 559 ms |
   | 4 | 2.51 | 1227 ms | **5.27** | **740 ms** |
   | 8 | 2.52 | 1249 ms | **5.22** | **744 ms** |

   扩展比从封顶 **1.42× → 2.78×**；每查询 CPU 从 +122% 降为 +43%。
   存储层裸调用：8 线程吞吐 **0.65 → 4.41 M/s（6.8×）**、CPU/查找 **4.59 → 1.68 µs**。

   **残留（另案）**：即使去掉锁，存储层 CPU/查找在 8 线程下仍上升 +150~178%，
   perf 显示主要在 `__wt_row_search` / `__wt_hazard_set_func`——属 WT 内部并发成本，
   与本次全局锁无关，需单独立项。
4. **风险与验证**：涉及并发语义，需（a）确认同一 session 不会被两个线程同时使用——
   查询内存储调用是顺序 await，每查询至多一个在途调用；（b）改造后用
   `tests/tools/concurrency_bench.cpp` 做同批交错 A/B 验证吞吐回升；
   （c）跑全量 ctest 与 Bolt/驱动验证确保语义不变。

**注意**：此前 §9.1–9.5 花大力气排查的引擎层方向（逐行取值链、批量形状、规划器 spec 选择）
全部不是并发退化原因——它们是**单线程性能**问题（谓词属性 2.05 次取用/行等），与并发退化是
两个独立议题，不应混淆。

## 9.8 谓词多属性的性能损失（确定存在，约 9 倍于投影）与下阶段清单

### 9.8.1 确定结论：谓词里每多一个属性 ≈ +235 ms/查询

同批 5 轮取 min（complex-9 的展开结构，117k 行，`consume()` 口径；去锁后重测）：

| 变体 | min | 相对 L0 |
|---|---:|---:|
| L0 不读属性 | 270 ms | — |
| L1 **谓词 1** 属性 | 504 ms | **+234** |
| L3 **谓词 3** 属性 | 973 ms | **+703**（≈ +235/属性） |
| P1 **投影 1** 属性 | 742 ms | +472 |
| P3 **投影 3** 属性 | 795 ms | **+53**（多 2 个属性只多 53 ms ≈ +27/属性） |

⇒ **谓词侧每属性 ≈ +235 ms，投影侧每属性 ≈ +27 ms，相差约 9 倍**。对 complex-9 这类
"3 谓词属性 + 3 投影属性"的查询，**谓词侧的额外成本（~700 ms）就是单查询耗时的主体**。

机制（§9.1/§9.4 已实测）：谓词属性经 `LoadVertexPropCoalesce`，是**每个候选 label × 每行
各读一次**（实测 2.05 次属性读/行/谓词属性）；而投影侧走整顶点物化的批量路径
（`need_whole_vertex` 分支在 `column_rewrite.cpp` 里直接跳过逐属性加载），一次读回该顶点
全部属性，故多属性近乎免费。修法须让谓词属性走单属性 spec，但谓词属性候选天然跨多 label
（`Post`/`Message`/`Comment` 属性空间不同），且 `evalPropertyRef` 对 `VertexRef` 目前只处理
`n.id` 结构字段（`src/query/evaluator/expr/eval_property.cpp:144`）——**属需设计评估的改动**。

### 9.8.2 已尝试但**无端到端收益**（勿重做）

| 改动 | 隔离微基准 | 端到端（同批 5 轮 min/median） |
|---|---|---|
| 投影分支单属性时免去 `Properties` 分配 | **1.015 → 0.749 µs/行/属性（+27%）** | L1 503.6→502.8、L3 973.5→992.3、P1 741.6→745.5、P3 795.3→798.8 ⇒ **噪声内，已回退** |

该分配开销**真实存在**（隔离可测），但在端到端被稀释到不可测——**不建议再单独做**；
若将来谓词路径改造后该路径占比上升，可重估。

### 9.8.3 下阶段清单（并发已修，剩余为"单查询成本"与"WT 内部并发"两块）

1. **谓词多属性**（§9.8.1）：预期最大单项收益（complex-9 可省 ~700 ms）；
2. **WT 内部并发成本**（§9.7 残留）：去锁后 8 线程 CPU/查找仍 +150~178%，
   perf 指向 `__wt_row_search` / `__wt_hazard_set_func`——需查页级/hazard pointer 争用；
3. **与 neo4j 的绝对差距**：每行 1.8 µs vs 0.15 µs（~12×）；并发扩展比我们 2.78×
   vs neo4j 3.50×（4 线程），已大幅缩小但仍有差距。

## 10. 官方 driver 跑 eugraph 时 id 必须按 `id_type` 传（否则全部空转）

官方 LDBC driver 的 `Converter.convertId()` 是 `Long.toString(value)`，**所有 id 参数以字符串发出**；
而 eugraph 的 id 是 INTEGER 且不做隐式转换：

```
MATCH (p:Person {id: '32985348834013'})  ->  0 行      4 ms
MATCH (p:Person {id: 32985348834013})   ->  116958 行 526 ms
```

**影响**：此前用官方 driver 对 eugraph 的跑分全部是"空查询"（含曾记录的
LdbcQuery9 3.9 s 与 `TOO_MANY_LATE_OPERATIONS`），结论作废。

**修法**：在 `benchmark.properties` 声明 `id_type=long`（eugraph）/ 默认 string（neo4j）。
driver 侧需三处小改（见 `docs/benchmark/ldbc-snb-sf0.1-comparison.md` §2.3.7）：
`Converter.convertId()` 可配置、`CypherQueryStore.setIdType()`、`CypherDb.onInit()` 读属性。

## 11. ~~官方 driver 并发跑到 ~116 操作后卡死~~ —— 已复核：**不是引擎问题**（2026-09-26 更正）

**原记录**：官方 driver 跑到第 116 个操作后 16+ 分钟不返回、服务端持续烧 2 核，
曾据此判定"并发卡死"。

**复核结论：该判定错误。真正的现象是"driver 未执行完全部操作"，与引擎无关。**

证据链：

1. **第二次复现时服务端是空闲的**：抓 `/proc/<tid>/stat` + `gdb`，唯一处于 R 状态的
   `CPUThreadPool` 线程实际停在 `UnboundedBlockingQueue::try_take_for`（等任务的超时等待），
   并非在执行查询；`jstack` 显示客户端 4 个 `ThreadPoolOperationExecutor` 线程全部 park、
   main 在 `Spinner.powerNap` 自旋等完成。
   ⇒ **两侧都空闲**，不存在"引擎无限计算"。
2. **诊断日志佐证**：在 `handlePull` 加节流诊断（chunks / fetched / rows_in_chunk）后复现，
   卡住期间诊断**零新增**（30 s 内 135 → 135），即没有新的 PULL、流不在产出。
3. **缺失操作数是按比例的，不是固定卡在某一条查询**：

   | `operation_count` | 实际执行 | 缺失 |
   |---:|---:|---:|
   | 40 | **39** | 1（2.5%） |
   | 120 | **116** | 4（3.3%） |

   两次都通过审计（`PASSED SCHEDULE AUDIT`，16.6 / 15.3 op/s），但**不再返回**。
4. **与调度窗口无关**：加 `ignore_scheduled_start_times=true` 后仍然停在 116。
5. **根因指向测量配置**：`benchmark.properties` 里 `time_compression_ratio=0.001`
   表示按 **1000× 压缩时间**调度（40 个操作被要求在 0.04 s 内全部发起）。引擎达不到该节奏时，
   部分操作错过窗口，driver 随后一直等这些操作完成而不退出。

**修法（跑分时）**：把 `time_compression_ratio` 设为 1.0（不压缩）或显著放大，
并让 `operation_count` 与实际吞吐匹配；此时 driver 能跑完并正常退出。参考实现的 README 也
说明该值用于"在保住 95% 准时率的前提下追求最低压缩率"，需要按被测系统实测调整。

**对 §11 的处置**：本条不再作为 eugraph 的缺陷；引擎侧此前记录的"并发下吞吐封顶 ~2.8 ops/s"
（§9，L1 未命中 +47.5%）仍是真实且已定证的问题，但**不是**"卡死"。

> 教训：把"driver 不退出"直接读成"引擎卡死"是错的。判断服务端是否真在算，要看
> `/proc/<tid>/stat` 的**实时增量**与**线程状态**（R vs futex/epoll），不能只看累计 CPU
> 或"客户端还在等"。本条第一次的误判正是因为抓到的两个 100% CPU 线程**确实是**在跑查询，
> 但那是**正常但很慢**的工作，而不是死循环——两者需要靠"是否持续产生新工作"来区分。

## 12. 优化器 `Memo::copyIn` 无限递归 → 栈溢出（**main 上既有**，2026-09-27 定证）

**症状**：ASan 构建下跑完整 TCK，在**场景 142**
（`CREATE (hf:School {name: 'Hilly Fields Technical College'})`）时服务端崩溃：

```
==53097==ERROR: AddressSanitizer: SEGV on unknown address (WRITE)
    #0  eugraph::optimizer::Memo::copyIn(...)
    #1  eugraph::optimizer::Memo::copyIn(...)
    #2  ...  #36+ 全部是同一个 copyIn 返回地址
[server] *** Check failure async stack trace: ***
[run_tck] Failing due to server crash
```

同一返回地址在 `#1..#36+` 反复出现 ⇒ **单子节点分支的无限自递归**（栈写越界即 SEGV）。

**与本次并发改动的归因（已用判据排除）**：把本分支改动的 4 个文件
（`wt_store_base.{hpp,cpp}`、`wt_session.hpp`、`eval_binary_op.cpp`）全部回退到 `origin/main`
版本，用**同一 ASan 构建 + 同一 TCK 命令**重跑：

| 版本 | 崩溃场景 | ASan 报告 |
|---|---|---|
| 本分支（含并发修法 + 快路径） | 142 | `Memo::copyIn` 无限递归 |
| `origin/main`（回退我的改动） | **142** | **同样 `Memo::copyIn` 无限递归** |

⇒ **该崩溃是 main 上既有缺陷，与本轮改动无关**（位置在优化器，不涉及存储/session）。

**复现**（本地稳定复现，非偶发）：

```bash
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_INSTALLED_DIR=$PWD/build/release/vcpkg_installed \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build/asan --target eugraph-server tck_tests -j 8
ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:log_path=/tmp/asan" \
  python3 tests/tck/run_tck.py --server-bin ./build/asan/eugraph-server \
    --tck-bin ./build/asan/tests/tck/tck_tests --port 7730 --data-dir /tmp/tckci \
    --features third_party/openCypher/tck/features --features tests/tck/features-eugraph
```

单条 CREATE 在全新库上**不复现**（release 与 ASan 均正常）⇒ 触发依赖前 141 个场景累积的状态。

**已定位（2026-09-27，用深度+指针去重诊断实测）**：**不是环，是一条超长无环链**。

在 `Memo::copyIn` 入口加临时诊断（深度计数 + 按 variant 地址去重检测环 + 打印每层 variant 索引），
并用 `ASAN` 版复现（注意 `run_tck.py` **不收集服务端 stdout**，须手动起服务端并把日志落文件，
再用 `EUGRAPH_HOST/EUGRAPH_PORT` 直接跑 tck 二进制）：

```
[diag] no cycle but depth 600 -- long acyclic chain
depth=30 idx=13 children=1 addr=0x...96d80
depth=31 idx=13 children=1 addr=0x...98d80     ← 恰好 +0x2000
depth=32 idx=12 children=1 addr=0x...9ad80     ← 恰好 +0x2000
...（600+ 层，全部 idx=13 BoundCreateEdgeOp / idx=12 BoundCreateNodeOp）
```

* `variant_index=12` = `BoundCreateNodeOp`，`13` = `BoundCreateEdgeOp`（顺序见
  `src/query/planner/bound_logical_plan_fwd.hpp`）；
* 每层算子对象地址**恰好相差 0x2000（8 KB）** ⇒ 是一条**线性嵌套链**，600+ 层 ≈ 4.8 MB 计划对象；
* 链长随**已执行的 CREATE 语句数**增长（前 141 个场景累积）⇒ 崩溃点在场景 142 只是"压垮骆驼的
  最后一根稻草"，而**根因在计划构造侧：CREATE 算子被跨语句累积成一条无界链**；
* `copyIn` 本身无环（指针去重已排除），它只是递归遍历这条病态链时爆栈。

**因此修复方向**：查 CREATE 的计划构造（binder/physical planner）为何把多次 CREATE 串联成一条
嵌套链，而不是每语句独立计划；`copyIn` 可另加深度上限做为防御。

**参考（次相关）**：`getChildCount` 与 `copyIn` 对子节点的判定此前被怀疑不一致——

```cpp
// getChildCount：BoundCreateNodeOp 会检查是否真的还有子节点
} else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
    return (val && val->child.has_value()) ? 1 : 0;
} else {
    return val ? 1 : 0;          // ← 泛型：只要指针非空就算 1 个
}

// copyIn 泛型分支：摘除子节点后把 child 重置为默认 BoundScanOp
auto c = std::move(val->child);
val->child = binder::BoundScanOp{};   // ← 若该类型重置后仍被判为"有 1 个子节点"，即无限自递归
return c;
```

**下一步（定位精确触发链）**：用 `run_tck.py --keep-data` 保留 142 场景后的库，再逐个重放该场景
序列二分出最小语句集；或在 `copyIn` 加"深度上限 + 打印每层算子类型"的断言，直接看是哪类算子
构成无限链。

