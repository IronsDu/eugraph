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

* **每行成本**：我们 **1.8 µs/行**（525.7 ms / 286,744），neo4j **0.15 µs/行**（43.6 ms）——约 **12×**；
* **并发行为**：neo4j 三种形态**全部零膨胀**（0.95–1.11×），我们仅"纯顺序"接近（1.18×），
  一旦引入"每行取一个属性"就跳到 2.1–3.9×。

### 9.4 分层微基准：存储层不弱，大头在引擎层

直连 WiredTiger（绕开算子与协程，`tests/tools/attr_fetch_bench.cpp`，20k 样本）：

| 取法 | 单位成本 |
|---|---:|
| 标签扫描（基准） | 0.083 µs/行 |
| **点查单属性** | **0.42–0.46 µs/次** |
| 全属性前缀扫描（逐顶点） | **1.29 µs/次** |
| 批量前缀扫描（一次 dispatch 内 N 次扫描） | 0.74 µs/次 |

**两个结论**：
1. 存储层点查 **0.43 µs** vs neo4j 每属性约 **0.15 µs** ⇒ 存储层只差 **~3×**；
   而线上每行 1.8 µs 减去存储 0.43 µs = **引擎层额外 ~1.4 µs/行**（值构造、列写入、
   协程/派发、对象分配等）——**这才是 12× 差距的主要来源**；
2. 这同时解释了"前缀扫描方案为何失败"：1.29 µs **比点查贵 3 倍**，
   因为它对每个顶点都要 `search_near` 后顺序遍历该顶点所有属性行。

**待办（据此更新优先级）**：
1. **引擎层每行开销**（~1.4 µs/行）：拆解 `ProjectionExtract` 的取值→`Value` 构造→列写入链，
   看是否有可省的临时对象/拷贝；这比存储层更有空间（存储只差 3×，引擎差 ~9×）；
2. 并发膨胀：**待办**
1. 并发膨胀：验证 MLP 假说（对照"随机点查"与"顺序扫描"在 1/2/4 线程下的 cycles per instruction
   与 L2/L3 未命中——本机 `perf` 不支持 LLC 事件，可用 `perf stat -e cycles,instructions` 配合
   人为降低随机性做对照）；若成立，优化方向是**提高顺序性/批量预取**，而不是减少属性数；
2. 属性数效应：在点查路径上找摊销（游标复用已知无效；需查 `search_near` 的遍历项数）。

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
