# 在线索引构建 v2 设计（基于 main 重做：后台构建 + 变更表 + 构建闸门）

> 状态：**设计稿 v2（待评审）**，基线 `main@b52ca8f6`（`refactor(storage): split session/index out of storage/data` 之后）。
> 本文**取代** 2026-10 的旧稿（分支 `docs/online-index-build-design` / `feature/online-index-build`，PR #247，已废弃）。
> 旧稿的协议缺陷见 §1，被否决的具体做法见 §16；本文只保留仍然成立的部分，其余重新设计。
>
> 目标一句话：把 `CREATE INDEX` 从"同步扫描 + 依赖并发写入自己写对的运气"改成
> **"构建期写入进入变更表、构建器追赶、关闸排空后才 `PUBLIC`"**，并让所有写路径只认一个权威状态。

---

## 1. 为什么重做（旧方案的问题，不是实现细节）

旧分支（PR #247）方向正确，但**协议本身**与 main 的实际结构不匹配，且实现里有若干使核心承诺失效的缺陷：

| # | 旧方案 | 为什么不能用 |
|---|---|---|
| 1 | 关闸后写者**直写索引** | 违反"最终化线"：关闸后、排空前写入索引的新值，会被随后重放的**旧 delta 行**删除；旧文档 §20.19 已怀疑，但修补没有消除该时序 |
| 2 | **先翻 `PUBLIC` 再排空** | 读者可在 delta 未清空时看到 `PUBLIC`；最后一次重放失败 ⇒ "PUBLIC 但索引不全"，正是要根治的事故形态 |
| 3 | `txn` 改 **thread_local** 隔离 | 协程会在 compute 线程池迁移，thread_local 事务会丢；映射按对象指针做键永不擦除，地址复用会读到旧事务。main 已有 `GraphSession`，事务隔离单位应是**会话**，不是线程 |
| 4 | 闸门注册表**进程级、只按 `index_id`** | 每图 `next_index_id` 都从 1 开始 ⇒ 多图同号索引共用一个闸门；且闸门关闭后不重开 |
| 5 | 准入守卫**绑定到写调用**且 `releaseAll` 在 `co_return` 之后（死代码） | `inflight` 永不归零 ⇒ 每次收尾都靠 5s 超时兜底，"排空"实际没生效 |
| 6 | `CREATE INDEX` **改异步却没同步迁移 loader** | loader 的 `createPrimaryKeyIndexes` 在装载边之前建唯一索引，并依赖它**立即可用**来解析端点；异步化后不同步等待 ⇒ 边写入失败或索引不完整。v2 保留异步，但把"等待 PUBLIC"显式做进 loader 与测试工具（§13.2） |
| 7 | 只有 `CREATE` 两条路径接闸门 | `SET`/`REMOVE`/`DELETE`/`MERGE` 仍绕过准入 |
| 8 | 重建/ERROR 后**不清理残留索引表** | 残留条目会让新索引读到错误结果或误报唯一冲突 |
| 9 | "应用即删"声称用户事务不受影响 | WT 是乐观并发，后提交者回滚；**用户事务可能被打回重试**，这是需要度量的成本，不是可以假设为 0 的性质 |

结论：不是在旧分支上解冲突，而是**在 main 的会话模型上重写协议**。旧分支里可复用的只有：两维状态模型的思路、变更表的键布局选择、阶段划分、以及几处独立缺陷修复（§20 列出）。

---

## 2. main 现状（设计的起点）

| 事实 | 位置 | 对本设计的影响 |
|---|---|---|
| `CREATE INDEX` 在查询执行器里同步完成：建定义（`WRITE_ONLY`）→ 建表 → **分批**回填 → 置 `PUBLIC` | `src/query/executor/query_executor.cpp` | "单一大事务"与"提交失败不检查"**已经修好**：分批提交 + 失败置 `ERROR`。本设计不再重复这两项 |
| 回填期间并发写入**直接写索引表**（`WRITE_ONLY` 与 `PUBLIC` 都算 live） | `src/storage/index/index_maintenance.hpp`、各 DML 算子 | 回填"读属性 → 写条目"之间若有并发更新，会留下**过期条目**；规划器选中索引后**不再复核谓词**（`physical_planner.cpp` 在匹配完整时丢弃 Filter）⇒ 过期条目 = **错结果**。这是本设计要根治的核心问题 |
| 规划器已只使用 `state == PUBLIC` 的索引（7 处） | `src/query/physical_plan/physical_planner.cpp` | I1 在规划器侧**已满足**，无需"收紧判定"；需要补的是**读准入**（DROP 与在飞查询） |
| 语句会话模型：`SessionPool` 拥有 `GraphSession`，`SessionLease` 非拥有；`AsyncGraphDataStore` **构造即绑定会话**（`for_session`） | `src/storage/session/` | 后台构建可以**借自己的会话**，天然与查询隔离；事务/准入的生命周期单位 = `GraphSession` |
| 索引维护有 **7 条写路径** + 批量装载路径 | `create_node`/`create_edge`/`delete`/`merge`/`remove`/`set` 算子 + `storage/index/index_maintenance.hpp` | 准入必须收敛到**一个汇聚点**，逐算子改必然漏 |
| 索引定义持久化在 `GraphSchema`，`IndexState{WRITE_ONLY,PUBLIC,DELETE_ONLY,ERROR}`；`next_index_id` 每图单调递增 | `src/common/types/graph_types.hpp`、`src/storage/meta/` | `index_id` **不复用**（H15 的前提成立）；`DELETE_ONLY` 是遗留值，v2 不使用 |
| **边索引从未分配 `index_id`**（`createEdgeIndex` 构造 `idx_def` 时漏了 `next_index_id++`） | `src/storage/meta/async_graph_meta_store.cpp` | 既有缺陷：所有边索引 `index_id == 0`。P0 必修（否则任何按 id 路由的设计都不成立） |
| 图生命周期闸门 `GraphUsageGate` + `GraphLease` | `src/common/concurrency/graph_usage_gate.hpp` | 后台构建必须持租约，`DROP DATABASE` 才能等它退出 |
| loader：**先建索引 → 等 PUBLIC → 再装载**；边写入靠主键索引解析端点 | [loader-design.md](../program/design/loader-design.md) | `CREATE INDEX` 异步后，loader 必须**显式等待** PK 索引 `PUBLIC` 再写边（v2 同步修改 loader，§13.2） |

---

## 3. 目标与非目标

**目标**

1. 构建**不阻塞写入**：`BUILDING` 期间的新写入进入变更表，构建器追赶，不丢写。
2. **读路径只认完整索引**：`PUBLIC` 是完整性证明；未 `PUBLIC` 的索引永不参与计划。
3. **失败显式**：任何一步失败 ⇒ `ERROR` + 原因，绝不假 `PUBLIC`；宁可重建（false negative）不可错用（false positive）。
4. **写路径只有一个权威**：7 条写路径 + 批量装载走同一个准入/去向判定；不存在"某条路径漏接"。
5. **可观测、可取消、可恢复**：状态/阶段/进度/backlog 可见；`DROP`/关图/重启都有明确收敛路径。
6. **异步 DDL**：`CREATE INDEX` / `DROP INDEX` 只**投递任务并立即返回**，构建/清理在后台推进；观测面就是 `SHOW INDEXES`（含 `phase`/`progress`/`last_error`），**不引入专用等待过程**；loader 与测试轮询它直到 `PUBLIC`（v2 同步改 loader）。

**非目标（本期）**

* 不做崩溃续建（不持久化扫描水位；崩溃 ⇒ `ERROR`，重建）。
* 不做单个索引内的并行扫描（并发度是"多个索引之间"）。
* 不做零阻塞 `DROP`（要求安全 + 有界等待，不要求完全不阻塞读者）。
* 不改索引键编码与索引表布局（只新增变更表）；不改 `IndexKeyCodec`。
* 不引入外部任务系统/独立进程；构建任务在图进程内的专用线程上跑。

---

## 4. 状态模型：持久化 vs 运行时

**为什么拆两层**：`IndexState` 是持久化的、粗粒度的（重启后仍要能判断"这个索引能不能用"）；构建阶段、闸门、进度、读者计数是**进程内**状态（重启即失效）。把两者塞进一个枚举会导致"状态无法单独决定写路径"。

### 4.1 持久化（meta，`IndexState`，不重编号）

| 值 | 含义（v2） | 规划器 | 写路径 |
|---|---|---|---|
| `WRITE_ONLY` | **构建中**（v2 唯一的构建中表示） | 不可用 | 写变更表（受准入保护） |
| `PUBLIC` | 完整可用 | 可用 | 直写索引 |
| `ERROR` | 构建失败/重启残留 | 不可用 | **不维护**（丢弃该索引的维护） |
| `DELETE_ONLY` | 遗留值，v2 不产生 | 不可用 | 不维护（按 `ERROR` 处理） |

**对外显示**：`WRITE_ONLY` 是内部持久化值，**不再泄漏给用户**——`SHOW INDEXES.state` 显示 `BUILDING`，`ERROR` 显示 `ERROR`，`PUBLIC` 显示 `PUBLIC`。Neo4j 兼容过程 `db.indexes` 保持 Neo4j 词表（`ONLINE`/`POPULATING`/`FAILED`），`populationPercent` 改为取注册表进度。

### 4.2 运行时（进程内，`IndexRuntime`）

```text
Phase      : ABSENT | SCAN | CATCHUP | FINALIZE | PUBLIC | ERROR
Lifecycle  : ACTIVE | DROPPING | PENDING_PURGE | PURGING | PURGED
admission  : OPEN | CLOSED（atomic bool + inflight 二次复查；没有 CLOSING 三态）
inflight   : uint32（已准入但事务未结束的写者数）
readers    : uint32（正在扫描该索引的查询数）
progress   : 0..100 / delta_backlog / started_at / last_error（replay_rounds 未实现）
tables     : index_table, delta_table
public_generation : uint64（发布时刻的 schema_generation）
```

> 术语：`BUILDING`（构建中）是 `phase ∈ {SCAN, CATCHUP, FINALIZE}` 的**用户侧合称**，
> 不是独立 phase；写路径只关心准入是否关闭。

**映射规则**：`lifecycle == ACTIVE && phase == PUBLIC` ⇒ 规划器可用；`phase == SCAN/CATCHUP/FINALIZE` ⇒ 写路径受准入约束；`phase == ERROR`/`PURGING` ⇒ 不维护。`IndexState` 是它的持久化投影（`WRITE_ONLY`/`PUBLIC`/`ERROR`），二者由同一次状态迁移一同写入（先数据提交、后 meta 落状态，见 §8.3）。

> **以代码为准**：上面的 Phase/Lifecycle 集合与迁移规则以 `src/common/types/index_runtime.hpp` 为准；
> 设计早期提出的 CATCHUP 阈值、FINALIZE 回退、admission CLOSING 等**没有实现**，
> 逐条差异见 §4.4。

### 4.4 状态流转（as-built，权威）

#### 4.4.1 Phase 迁移（`indexPhaseTransitionAllowed`）

```text
ABSENT   --CREATE--> SCAN
ABSENT   --启动/失败--> ERROR
SCAN     --回填完成--> CATCHUP
SCAN     --失败/取消/schema 错误--> ERROR
CATCHUP  --进入 FINALIZE--> FINALIZE
CATCHUP  --失败/取消--> ERROR
FINALIZE --关闸/排空/校验通过 + meta PUBLIC--> PUBLIC
FINALIZE --校验失败/排空失败--> ERROR
PUBLIC   --DROP 只走 lifecycle，phase 不变--> PUBLIC
ERROR    --终态，重建 = 新 index_id + 新 runtime--> ERROR
```

* 实现里 **SCAN 结束后直接进入 FINALIZE**（FINALIZE 内部一次性把 delta 追到 EOF），
  没有“CATCHUP 阈值连续 2 轮”的独立阶段循环。
* `CATCHUP` 仍会被使用：`finalizeIndexBuild` 开始时把 phase 置为 CATCHUP，再置 FINALIZE，
  用于观测与测试钩子；不是长期驻留阶段。

#### 4.4.2 Lifecycle 迁移（`indexLifecycleTransitionAllowed`）

```text
ACTIVE        --DROP（异步）--> DROPPING
ACTIVE        --DROP（无构建服务的同步装配）--> PURGING
DROPPING      --旧 pin/readers 归零后删表成功--> PURGING --> PURGED --> erase
DROPPING      --等待超时/删表失败--> PENDING_PURGE
PENDING_PURGE --周期清扫/下次打开，readers==0--> PURGING --> PURGED --> erase
```

* 规划器要求 `lifecycle == ACTIVE`；`DROPPING/PENDING_PURGE` 只服务**已生成计划**的在飞读者
  （`acquireRead` 允许），`PURGING/PURGED` 拒绝新读句柄。
* 读准入：`acquireRead`（读句柄）覆盖“已经打开游标”的查询；
  `SchemaGenerationTracker` 覆盖“已 pin、还没打开游标”的事务。两者都归零才删表。

#### 4.4.3 持久化映射与启动

| 持久化 `IndexState` | 打开图时 runtime phase | 说明 |
|---|---|---|
| `PUBLIC` | `PUBLIC` | 可用；`public_generation=0`（对所有 pin 可见） |
| `WRITE_ONLY` | `ERROR` | 构建随进程死亡；本期不续建；半成品表登记 orphan 并在启动清理 |
| `ERROR` | `ERROR` | 不可用；定义保留，可 DROP 后重建 |
| `DELETE_ONLY` | `ERROR` | 历史状态，不再产生 |

#### 4.4.4 设计目标但当前未实现（避免文档/代码漂移）

| 设计目标 | 当前实现 |
|---|---|
| CATCHUP 阈值 + 连续 2 轮再 FINALIZE | 回填后一次性 `replayDeltaBatch` 到 EOF |
| FINALIZE 关闸超时回退 CATCHUP | 超时/失败 ⇒ `ERROR`（`failIndexRuntime`） |
| admission `OPEN/CLOSING/CLOSED_PUBLIC/CLOSED_ERROR` 四态 | atomic `open` bool + `inflight++` 后复查 + baton 等待 |
| PUBLIC 后异步 drop `idx_delta_<id>` | 已实现：PUBLIC 后 drop 空 delta；失败告警，DROP 兜底 |
| 构建持 `GraphUsageGate::Guard` | 未持；靠 `GraphManager` 先 shutdown 构建服务再关连接 |
| 唯一索引“CATCHUP 末尾全扫 + 最后一批 point-lookup”两段校验 | 回填逐行查重 + 关闸排空后一次全量相邻键扫描 |
| `version_refcount` 等旧版本引用 | 读句柄 `readers` + `SchemaGenerationTracker` pin 计数 |
| `replay_rounds` 指标 | 未采集 |


### 4.3 不变量（实现与评审逐条对照）

| # | 不变量 |
|---|---|
| **I1** | **规划器只用“事务 pin 里为 `PUBLIC` ∧ 注册表当前 `lifecycle == ACTIVE`”的索引**；执行时算子还要过一次读准入（§10）。两个检查回答不同问题：**能不能为“新计划”选它**（pin+ACTIVE）与**已在飞的计划能不能继续用**（数据是否还在）。 |
| **I2** | **每个已提交写入只有一个去向**：`BUILDING` ⇒ 变更表；`PUBLIC` ⇒ 索引表；不存在"部分写变更表、部分写索引"。 |
| **I3** | **最终化线唯一**：关闸成功前准入的写者一律写变更表且必须被排空；关闸后的写者**等待**，`PUBLIC` 后直写索引；**任何写者都不得在 `PUBLIC` 之前直写索引表**。 |
| **I4** | **`PUBLIC` 是完整性证明**：基础扫描完成 ∧ 变更表扫到 EOF 为空 ∧ 校验通过 ∧ 状态已持久化。 |
| **I5** | **准入与关闸线性化**：写者必须 `inflight++` **之后**复查闸门仍开，才算准入；一旦准入，收尾方必须等其事务结束。 |
| **I6** | **准入守卫的生命周期 = 用户事务**（= `GraphSession` 的事务作用域），在 commit/rollback/close 释放；**禁止 thread_local、禁止按写调用释放**。 |
| **I7** | **只有一个写入汇聚点**：所有维护索引的写入都经过 `IndexWriteRouter`；算子/批量路径不得自行调用 `insertIndexEntry`/变更表写。 |
| **I8** | **闸门标识 = (graph_id, index_id)**，每图一张注册表；每次构建开始**复位**闸门；失败/取消不得留下永久关闭的闸门。 |
| **I9** | **失败方向**：崩溃/异常/超时一律落到"不可用（`ERROR`/非 `PUBLIC`）"，绝不落到"可用但不完整"。 |
| **I10** | **会话不共享**：后台构建只用自己租到的 `SessionLease`；同一 `GraphSession` 不得跨线程并发使用；禁止把事务句柄放进 TLS。 |
| **I11** | **事务级不可变 schema**：事务 BEGIN 时拿一个 `shared_ptr<const GraphSchema>` 并持有到结束；schema 的新版本对它在整个事务内不可见。索引可用性同样冻结：只有 `public_generation ≤ start_generation` 的索引可被它选中。写路径按注册表当前状态路由。理由见 §10.1/§10.3。 |
| **I12** | **提交校验按“本事务写到的元素”**：写事务提交时，其 touched 的标签/属性必须仍存在且兼容；否则以可重试错误拒绝（不看版本距离）。索引增删不构成提交冲突。理由见 §10.2。 |
| **I13** | **两个计数器各司其职**：`schema_generation` 每次**定义发布** +1（只用于记账/快照识别/drain）；`compat_version` 只在**不兼容**变更（删标签、删属性、改类型、改主键）+1。兼容新增（新标签/加属性/新索引/状态翻转/统计）**不递增兼容版本**。 |

---

## 5. 运行时注册表（每图一个，写路径/读路径的共同权威）

```cpp
// src/storage/index/index_runtime_registry.hpp（示意）
struct IndexRuntime {
    uint32_t index_id;
    LabelId label_id;                 // 顶点或边标签
    std::vector<uint16_t> prop_ids;
    bool unique;
    bool is_edge;
    std::string index_table;          // vidx_<id> / eidx_<label>_<prop> 的实际表名（创建时解析一次）
    std::string delta_table;          // idx_delta_<id>

    std::atomic<Phase>   phase{Phase::kAbsent};
    std::atomic<Lifecycle> lifecycle{Lifecycle::kActive};
    std::atomic<bool>    admission_open{true};
    std::atomic<uint32_t> inflight{0};
    std::atomic<uint32_t> readers{0};

    std::atomic<double>  progress{0};
    std::atomic<uint32_t> delta_backlog{0};
    std::chrono::steady_clock::time_point started_at;
    std::mutex error_mu; std::string last_error;
};

class IndexRuntimeRegistry {   // 每图一个
public:
    std::shared_ptr<IndexRuntime> find(uint32_t index_id) const;
    std::shared_ptr<IndexRuntime> createOrUpdate(const IndexDescriptor&);
    void erase(uint32_t index_id);                 // 仅在 PURGING 完成后
    std::vector<std::shared_ptr<IndexRuntime>> forLabel(LabelId) const;   // 写路径用
    bool currentUsable(uint32_t index_id) const;                          // 当前是否 PUBLIC∧ACTIVE（观测/新事务）
    bool plannerSelectable(uint32_t index_id) const;                     // 计划期：当前 lifecycle==ACTIVE（§10）
    std::optional<ReadHandle> acquireRead(uint32_t index_id);             // 执行期读准入（§10：DROPPING/PENDING_PURGE 允许）
};
```

**要点**

* **权威性分工**：**写路径与读准入的当前状态**以注册表为准；**读计划的可见性**以事务绑定的元数据版本为准（I11，§10.1）。`GraphSchema` 仍是定义的持久化真相，注册表在**图打开**、`CREATE INDEX`、`DROP INDEX`、状态迁移时由同一临界区更新。
* **线程安全**：注册表自身一把读写锁（只保护 map 的增删查）；每个索引的运行时字段是原子量，不持锁读。
* **"新索引对 DML 立即可见"**：`CREATE INDEX` 在**返回前**把 `IndexRuntime` 插入注册表（`phase=SCAN`）。DML 的收集器改为**遍历注册表**（按 label），而不是遍历语句计划期的 `label_defs` 快照 ⇒ 旧方案里"构建期新建索引对 DML 不可见 ⇒ 既不写索引也不写 delta ⇒ 丢写"的问题在结构上消失。
* **多图隔离**：注册表由 `GraphInstance` 持有，键只在本图内有效；不存在进程级共享（I8）。
* **`index_id == 0` 防御**：注册表 insert 拒绝 0，并在 `createEdgeIndex` 修好 id 分配之前，启动时把边索引 id 为 0 的定义视为 `ERROR` 并告警（防止路由到 `idx_delta_0`）。

---

## 6. 写路径：一个汇聚点 + 会话级准入

### 6.1 汇聚点

```cpp
// src/storage/index/index_write_router.hpp
struct IndexWriteOutcome { int unique_conflicts = 0; bool ok = true; std::string error; };

IndexWriteOutcome applyVertexIndexEntries(ISyncGraphDataStore& store, GraphSession& session,
                                          IndexRuntimeRegistry& registry,
                                          const std::vector<VertexIndexEntry>& entries,
                                          const IndexWriteOptions& opts);

IndexWriteOutcome applyEdgeIndexEntries(ISyncGraphDataStore& store, GraphSession& session,
                                        IndexRuntimeRegistry& registry,
                                        const std::vector<EdgeIndexEntry>& entries,
                                        const IndexWriteOptions& opts);
```

* 收集器（`storage/index/index_maintenance.hpp`）的产物改为 **`{index_id, values, entity_id, payload}`**，不再携带表名；表名由 router 从注册表解析（单一来源，避免"表名/ID 各算一遍"的错配）。
* 7 条 DML 路径 + 批量装载（`batchInsertVertices`/`batchInsertEdges`）都必须调用 router；**移除**它们各自对 `insertIndexEntry` / `indexIsLive` 的直接依赖（`indexIsLive` 删除）。
* 唯一性检查只在 `phase == PUBLIC` 时做（构建期不做，见 §8.4）；unique 冲突语义保持现状（跳过该条 + 计数）。

### 6.2 每个索引的准入与去向

**分层约束**：router 在 **sync 存储层**执行（它被 `GraphSession` 的既有事务包裹，不能自己 begin/commit），
因此它**不做任何等待/睡眠**——那会阻塞 IO 线程。等待发生在 **async 层**（协程），用协程 sleep 实现。

router 对每个索引返回三种结果之一：

```text
rt = registry.find(index_id)
switch (rt->phase):
  SCAN / CATCHUP（即"构建中"）:
      guard = rt->tryEnter(session);       // 两阶段：inflight++ 后复查 admission_open
      if (!guard) -> 返回 RetryLater        // 不等待、不直写
      write delta_table                     // 与实体写入同一事务
      返回 Applied
  FINALIZE:
      -> 返回 RetryLater
  PUBLIC:
      write index_table                     // 与实体写入同一事务（现状行为）
      返回 Applied
  ERROR / PURGING / ABSENT:
      返回 Skipped（不维护；索引不存在 ⇒ 不报错）
```

**async 层的重试（I3/I9 的关键）**

```text
deadline = now + index_write_wait_ms（默认 1000ms，可配；按语句计）
loop:
  outcome = router(entries)                 // 一次调用处理本条语句涉及的全部索引
  if outcome != RetryLater: return outcome
  if now > deadline: 返回可重试错误（语句失败，错误信息含索引名与状态）
  co_await folly::coro::sleep(1ms)           // 协程睡眠：不占计算/IO 线程
```

* 重试时 router 会**重新读注册表**：若期间已 `PUBLIC` ⇒ 直接写索引；若已 `ERROR` ⇒ 跳过。
* 多个索引时，router 先按 `index_id` 升序逐个准入；已准入的索引在本次重试里会再次命中
  （`tryEnter` 对同一 session 幂等——同一索引的守卫只登记一次，见 §6.3），不会重复计数。

**禁止**：`admission_open == false` 时直写索引表（旧方案的做法）。等待有界、失败可见，**不猜**。

### 6.3 准入守卫的归属（I6）

* `GraphSession` 增加 `std::vector<IndexWriteGuard> index_admissions_;`
* `tryEnter(session)` 成功即把守卫移入该 session；`GraphSession::commit()/rollback()/close()` 统一 `releaseIndexAdmissions()`（释放 ⇒ `inflight--`）。
* **幂等**：同一 session 对同一 `index_id` 重复准入只登记一次（按 `index_id` 查已有守卫；`inflight` 只加一次），
  这样语句内多条实体写入、以及 async 层因 `RetryLater` 重试都不会重复计数。
* 因此：**显式事务（BEGIN…COMMIT）跨语句**时，守卫覆盖整个用户事务；自动提交语句在语句结束时释放。两种情形都由会话正确承载，不依赖线程或 TLS。
* 守卫是 move-only、不可复制；重复释放/未准入释放由 `Guard` 自身保证不会发生（`std::shared_ptr<IndexRuntime>` + 一次性标志）。

### 6.5 示例：构建期一次 UPDATE 的去向（物理位置）

假设 `idx_email`（`Person.email`）正在 `BUILDING`，事务 T 把 `person#42` 的 email 从 `a@x` 改成 `b@x`：

1. 收集器从**注册表**拿到 `idx_email`（不看 T 的 schema 快照 ⇒ T 即使开始于建索引之前也能维护它）；
2. router 读注册表：`phase=SCAN`、闸门开 ⇒ `tryEnter(session)` 成功，守卫挂到 T 的会话（事务结束才释放）；
3. **同一事务**内写两行到 `idx_delta_<id>`：
   * `encodeKey(a@x, 42)` → `{op=DEL}`
   * `encodeKey(b@x, 42)` → `{op=PUT}`
4. `person#42` 的数据行也在同一事务里更新 ⇒ **commit 一起可见、rollback 一起消失**；
5. builder 的 CATCHUP 按键序扫到这两行：删索引里的 `a@x→42`（若存在）、写 `b@x→42`，并在**同一 builder 事务**里删掉这两行；
6. T 结束 ⇒ 守卫释放 ⇒ `inflight--`；FINALIZE 只有等 `inflight==0` 且 delta 扫到 EOF 才翻 `PUBLIC`。

**物理位置**：变更表在**同一个图的 WT home（数据 store）**里，是一张普通 B-tree 表，不是内存缓冲、不是 WAL、更不是索引表。
因此它天然具备事务性（回滚/崩溃一致），崩溃后按 §9.1 的策略整索引重建（不做续建）。

**反例（禁止）**：把 delta 写在事务之外（回滚后留幽灵行）、或先写索引表再补 delta（与回填事务写冲突并留下过期条目）。

### 6.4 与"实体写入同一事务"

变更表写入与实体写入使用**同一个 `GraphSession` 事务**（router 只拿到 session，不做 begin/commit）。这保证：回滚 ⇒ delta 行一起消失；提交 ⇒ 一起可见。若某条写路径当前是"逐条提交"（例如批量装载的分批提交），那么每批内一致即可（B 路径天然分批）。

---

## 7. 变更表（delta）设计

### 7.1 布局

| 项 | 设计 |
|---|---|
| 表 | 每索引一张 `idx_delta_<index_id>`（在数据 store 内，随图目录回收） |
| 键 | `IndexKeyCodec::encodeIndexKey(values, entity_id)`（**索引键在前、实体 id 在后**） |
| 值 | `index_delta_codec`：`{op: u8 (PUT|DEL), payload: bytes}`（payload 为边索引邻接值，顶点为空） |
| 去重 | 同一 `(key, entity)` 覆盖写 ⇒ 每对最多一行；表大小 ∝ 被改动的键数 |
| 键变更 | 旧键 `DEL` + 新键 `PUT`（同一事务两行） |
| 墓碑 | `DEL` 无条件记录；重放"删不存在的条目"是 no-op ⇒ 幂等 |
| 事务 | 与实体写入同一事务（§6.4） |

**为什么是 B+tree 而不是 append-only 日志**：按键序 ⇒ 重放是"有序、页内局部性好"的写入；天然去重；唯一性自查只需比较相邻键。append-only 需要额外排序/聚合，且体积随写入次数无界增长。（与旧稿结论一致。）

### 7.2 重放（`replayDeltaBatch`）

```text
batch = 1024 行（可配）
同一 builder 事务内：
  scan delta 按键序，从 start_after 续扫
  对每行：PUT ⇒ insertIndexEntryByKey(index_table, key, entity, payload)
          DEL ⇒ deleteIndexEntryByKey(index_table, key, entity)
  应用后**删除该 delta 行**（同事务）
commit；若 WT_ROLLBACK ⇒ 整事务重试（重新从 start_after 开始）
返回 applied 行数与 last_key，供下一批续扫
```

* **为什么应用后删**：变更表始终只装"未应用的变更"，长构建不会把它撑大；收尾时"扫到 EOF 且无行"即可判定清空。
* **冲突语义（必须实测，不得假设）**：builder 与用户事务可能写同一 `(key, entity)`。WT 乐观并发下**后提交者回滚**——可能是用户事务。要求：
  * builder 每批事务短（≤ 1024 行），冲突窗口小；
  * 用户事务遇 `WT_ROLLBACK` 走既有重试路径（`TxnErrorClass::is_retryable`）；
  * 记录 `delta_replay_conflict_total` 指标；压测判据见 §13.3 的 T9。若出现**用户可见的重试/失败**，备选方案见 §19.2（值带 seq + 追赶期不删行）。
* **不持久化水位**：builder 每轮从表首或上次 `last_key` 续扫；崩溃即整索引重建（§9 的恢复策略），因此不需要水位。

### 7.3 清理

| 时机 | 动作 |
|---|---|
| 构建成功置 `PUBLIC` 后 | 异步 `drop` 变更表（幂等；失败登记孤儿表） |
| 构建失败置 `ERROR` | 立即 `drop` 变更表 + `drop` 部分索引表 |
| 构建中被 `DROP` | 先取消并等 builder 退出，再 `drop` 两张表 |
| 启动时发现 `WRITE_ONLY` | 置 `ERROR`，`drop` 变更表与部分索引表（同名表不存在 ⇒ 视作成功） |

---

## 8. 构建流程与最终化协议

### 8.1 阶段

```text
SCAN      扫描基础数据（分批、每批独立事务、快照事务读）
          ⇒ builder 是 BUILDING 期**索引表的唯一写者**（DML 全走 delta）
          ⇒ 无写写冲突、无过期条目、无需水位
CATCHUP   多轮重放变更表，直到 backlog ≤ 阈值（连续 2 轮）
FINALIZE  关闸 → 等 inflight 归零 → 最后一次扫到 EOF → 校验 → 置 PUBLIC
PUBLIC    开闸（PUBLIC 模式）→ 异步 drop 变更表
```

**as-built**：当前实现是 `SCAN` 回填完成后直接进入 `FINALIZE`（`finalizeIndexBuild` 内把 delta 一次性追到 EOF），
没有“CATCHUP 阈值连续 2 轮”的独立阶段；`CATCHUP` 只作为 FINALIZE 的入口 phase 短暂出现。

**旧事务 drain（必须）**：服务端构建在 SCAN 开始前调用
`SchemaGenerationTracker::waitNoPinsOlderThan(index_generation)`，等“索引定义发布前 BEGIN”的事务结束。
这些事务的 schema 快照里没有新索引，写路径不会写 delta；不 drain 就让 SCAN 先跑，会漏掉它们后提交的写入。

**分块扫描的正确性论证**：每批用各自快照读；快照之前提交的数据由扫描覆盖，快照之后提交的数据必然在变更表里（因为写路径按注册表状态分流）。二者互补不重叠；UPDATE 换键写"旧键 DEL + 新键 PUT"，重放幂等 ⇒ 结果与最终状态一致。

**唯一索引**：扫描阶段**不做**唯一性检查（并发写入互相不可见，做也无效）；见 §8.4。

### 8.2 进入 FINALIZE 的前置条件（缺一不可）

1. `delta_backlog ≤ delta_threshold`（默认 1000 行）**连续 2 轮**；
2. 最老在飞写事务年龄 ≤ `oldest_inflight_ms`（默认 200ms）——防止"长事务 ⇒ 关闸 ⇒ 排空超时 ⇒ 回退"的抖动循环；
3. builder 依赖的元素未变（被索引的标签/属性未被删或改类型，§11.3）——**不是**"全局版本没变"。

> **as-built**：以上 1/2 未实现。当前回填结束即 `closeAndWait(5000ms)`，
> 超时直接 `ERROR`（不回退 CATCHUP）；第 3 条依赖尚无破坏性 DDL，未接线。

不满足 ⇒ 留在 CATCHUP 继续重放并记录 `finalize_deferred_total`。

### 8.3 最终化（严格顺序，不可颠倒）

```text
1. CAS admission: OPEN -> CLOSING；此后 tryEnter 一律失败，写者进入等待循环
2. 等待 inflight == 0
     超时（drain_timeout_ms，默认 5000）⇒ 回退：admission=OPEN，phase=CATCHUP，告警 + 指标
3. 重放变更表到 EOF（短事务；应用后删行）
4. 校验（全部通过才允许继续）：
     a. 变更表为空（扫到 EOF，不用计数器）
     b. 唯一索引无冲突（§8.4）
     c. 空索引护栏：基础扫描计数器 `scanned_with_all_props > 0 ∧ emitted == 0` ⇒ 失败
5. 持久化：meta 写 state = PUBLIC（数据已提交之后）
6. admission = CLOSED_PUBLIC；phase = PUBLIC；progress = 100
7. 异步 drop 变更表（幂等；失败登记孤儿）
任一失败：phase = ERROR，meta 置 ERROR，admission = CLOSED_ERROR，drop 变更表与部分索引表
```

> **as-built**：`admission` 只有 open/closed（外加 baton 等待）；关闸超时或失败直接 `ERROR`（不 CATCHUP）；
> 成功路径 PUBLIC 后会 drop 空的 delta 表，失败只告警、DROP/启动回收兜底；
> 失败路径的部分表交给 orphan 记录/启动回收。

**为什么 `PUBLIC` 只能在 3、4 之后**：`PUBLIC` 对读者是"完整性证明"；先翻状态再排空会让读者在缺口窗口看到不完整索引（旧方案的问题）。校验失败也必须走 `ERROR`，不得"先上线再补"。

**为什么失败要开回 CATCHUP 而不是直接 ERROR**：排空超时通常是"有慢事务"，不是索引坏了；让业务写继续（重新写 delta）、构建稍后再试，比把索引打死更符合可用性目标。但**schema 变化 / 校验失败 / 提交失败**是确定性故障 ⇒ 直接 `ERROR`。

### 8.4 唯一索引的构建期语义

**一句话**：构建期间**先不保证唯一**，构建完成时**一次性验证**；发现重复 ⇒ 构建失败（`ERROR`，索引不可用），
**绝不带着重复键上线**。这与 PostgreSQL `CREATE UNIQUE INDEX CONCURRENTLY` 的语义一致。

**具体行为**

| 阶段 | 唯一性 |
|---|---|
| `BUILDING`（SCAN/CATCHUP/FINALIZE） | **不检查**。写入照常成功（写 delta），即使两个实体用了同一个键 |
| 构建完成校验 | 对最终态（索引 ⊕ 最后一批 delta）验证；有重复 ⇒ **构建 `ERROR`**，报告冲突键与实体 id |
| `PUBLIC` 之后 | 正常强制：写入先查索引，冲突拒绝（现状行为） |

**为什么不能构建期强制**：两个并发事务互相看不到对方未提交的写入，"先查再写"两边都会通过（H5）；要真正保证唯一只能给
每个键加锁或做预留行，代价是把写路径串行化、并且事务会被打回重试——不值得。构建期写 delta 的设计让"重复"在这一阶段
是**允许的中间态**，由最终校验兜底。

**实际影响**

* **工作流 A（先建索引 → 空表 → 再装载）**：空表构建瞬间 `PUBLIC`，之后装载全程强唯一，**不会**遇到这个问题（loader 走这条）；
* **给已有数据补唯一索引**：如果数据里本来就有重复，或构建期间写入制造了重复 ⇒ 构建 `ERROR`；
  清理重复后重新 `CREATE INDEX` 即可。错误信息里给冲突键与两个实体 id，便于定位。

**最终态校验**分两段，避免在闸门内做 O(索引大小) 的全扫描：
  1. **CATCHUP 末尾做一次全量相邻键扫描**（不持闸门）：索引按键序，同键不同实体 ⇒ 冲突。若发现 ⇒ 直接 `ERROR`（尽早失败）。
  2. **关闸排空后只对"最后一批 delta 涉及的键"做 point-lookup + 批内排序**：最终态 = 扫描完成时的索引 ⊕ 最后一批 delta；任何新增冲突必然涉及最后一批的键，因此这一遍是**完备**的。
  3. 两段都通过才允许 `PUBLIC`。
* **`PUBLIC` 之后**：写路径按索引查重，冲突拒绝写入（现状语义）。

> **as-built**：回填阶段逐行 `check_unique`（能尽早发现历史重复）；关闸排空后
> `indexHasDuplicateKeys` 对**整个索引表**做一次相邻键扫描（O(索引大小)），未实现
> “只查最后一批 delta 键”的两段优化。构建期并发写入制造的重复由这道全扫兜底。

### 8.5 空索引护栏

扫描计数器：`scanned_with_all_props` 与 `emitted_entries`。若 `scanned_with_all_props > 0 && emitted_entries == 0` ⇒ 失败（直接拦住历史事故"0 条却 ONLINE"）。该判据不依赖估算计数，成本为 O(1)。

---

## 9. 生命周期：启动、崩溃、关图、DROP

### 9.1 启动恢复（每个图打开时）

```text
for label in schema.labels,      for idx in label.indexes:        recover(idx)
for elabel in schema.edge_labels, for idx in elabel.indexes:      recover(idx)

recover(idx):
    if idx.state == WRITE_ONLY:             # 上次构建未完成
        idx.state = ERROR（保留定义，告警）
        尽力 drop idx_delta_<idx.id> 与部分索引表（失败 ⇒ 登记孤儿）
    if idx.index_id == 0:                   # 边索引历史缺陷的防御
        idx.state = ERROR；告警（P0 修复后不再产生）

过期清理：meta 里登记的孤儿表逐条重试 drop（成功即销账）
```

**可见行为变化**：以前重启后索引会**永远停在 `WRITE_ONLY`**（不可用且无法自愈）；v2 变成 `ERROR`（可 `DROP` 后重建）。这是修复，不是回归；在 [index_design.md](index_design.md) 与 `SHOW INDEXES` 文档中说明。

**重启时会看到什么（按崩溃点）**

| 崩溃发生在 | 持久化状态 | 重启后 | 用户操作 |
|---|---|---|---|
| SCAN / CATCHUP 中途 | `WRITE_ONLY` | `ERROR`；delta 与部分索引表被删；规划器不用 | `DROP INDEX` 后重建 |
| FINALIZE 中（数据已提交、状态未翻） | `WRITE_ONLY` | 同上——**即使索引数据其实已完整，也不置 `PUBLIC`** | 同上 |
| 已落 `PUBLIC` 之后 | `PUBLIC` | 保持 `PUBLIC`，正常可用 | — |
| `DROP` 过程中 | 定义可能已删 | `pending_drop`/孤儿记录继续清理 | — |

**不会在后台继续构建**：构建任务只活在进程内存里（队列/协程/闸门都是进程内状态），重启后没有任何构建在跑；
`SHOW INDEXES` 显示 `ERROR`（定义保留、`last_error` 说明重启中断），必须重建。原因：

1. 本期**不持久化扫描水位**——没有水位就无法证明"基础扫描扫到哪一行"，续建可能漏行；
2. 崩溃后"基础扫描 + delta"的互补边界已不可复现（delta 行数不能证明覆盖）；
3. 安全方向（I9）：宁可 false negative（重建）不可 false positive（假 `PUBLIC`）。

代价是重建要重扫一遍大标签；续建的可行方案见 §19.5（本期不做）。

### 9.2 孤儿表

* `DROP INDEX` / 失败清理时，若 `dropIndex(table)` 因"表被会话占用"失败：**定义照常删除**（语义已生效），把表名持久化到 meta（`M|orphan:<table>`），告警；图**下次打开**时重试（此时旧进程的会话已消失，通常必成）。
* 为什么不当场反复重试：会话按线程缓存、寿命≈进程寿命，同进程内重试基本无效（旧稿 §20.22 已实测）。
* 为什么不能"启动时枚举所有表"：本仓库的 WT 版本没有 `get_table_names`；只能靠显式登记。

### 9.3 `DROP INDEX`（异步 + 两阶段）

`DROP INDEX` 是**异步 DDL**：语句只做"标记 + 持久化待清理记录 + 删定义 + 返回"，物理清理在后台完成。

```text
1. registry.lifecycle = DROPPING          // 规划器立刻不再选它
2. meta 持久化 pending_drop 记录           // M|pending_drop:<id>：索引表 + 变更表（**先记后删**）
3. meta 删除索引定义                        // 立即生效：SHOW INDEXES 不再显示；同名索引可立即重建
4. 返回 "Index dropped (dropping): <name>"
5. 后台清理：
     a. 若在构建 ⇒ cancel + 等 builder 退出（先开闸）
     b. 等**引用该索引的旧元数据版本**全部释放（`version_refcount == 0`；它覆盖 `readers == 0`：
        事务可能先拿到版本、稍后才扫这个索引）。超时 purge_wait_ms ⇒ 转 PENDING_PURGE 继续等
     c. drop 索引表 + 变更表（失败 ⇒ 转孤儿表登记）
     d. 删除 pending_drop 记录 + registry.erase
```

* **为什么先记 pending_drop 再删定义**：定义一删，表名就只能从记录里恢复；崩溃落在 2–5 之间时，启动恢复靠它继续清理（幂等）。
* **在飞事务仍能用它**：事务持有的元数据版本里还有该定义（§10.1），读句柄保数据到 `readers==0`；写路径在 `DROPPING` 期间继续**直写索引**（否则在飞长查询会读到缺最新写入的索引）。
* **两种准入要分开**：
  * **计划期**（选索引）：要求 pin 里 `PUBLIC` **且**注册表当前 `ACTIVE` ⇒ `DROP` 之后**新计划一律不选它**（老事务里的新语句也一样）；
  * **执行期**（算子打开游标）：`acquireRead(index_id)` 只要求数据还在 ⇒ `DROPPING`/`PENDING_PURGE` 允许、`PURGING` 拒绝并返回可重试错误；
    已在飞的查询因此能跑完且结果正确，而新查询不会“捡”到它。
* **DROP 的返回时机 ≠ 线性化点**：语句返回（异步）后定义已删除、`lifecycle=DROPPING`，此时**新查询已经不可能选到它**；
  物理表可能还在（等旧版本引用归零），只服务于已生成计划的在飞查询。
* **构建中的索引被 DROP**：没有读者；同样走上面的流程，`cancel` 后直接删表删定义，不进入两阶段等待。
* **唯一约束的失效边界**：进入 `DROPPING` 的线性化点（不是物理删除完成）；在飞写事务按其准入时的规则处理。写入 API 文档与 `SHOW INDEXES` 需说明。
* **崩溃恢复**：`DROPPING`/`PENDING_PURGE`/pending_drop/孤儿表在启动时继续清理（幂等；表不存在视作成功）。

> **as-built**：没有 `version_refcount`；旧版本事务用 `SchemaGenerationTracker` 的 pin 计数
> （`waitNoPinsOlderThan`）+ 算子读句柄 `readers`，两者归零后删表。清理任务超时转
> `PENDING_PURGE` 并保留 pending_drop，由 5s 周期任务/下次打开重试。

### 9.4 关图与 DROP DATABASE

* 每图一个 `IndexBuildService`（§12），`GraphInstance` **在 `executor` 之后**声明它 ⇒ 反向析构时先排空。
* `GraphManager::closeAll()` / `dropGraph()`：**先 `index_builds->shutdown()`（cancel + join）再关 store**。
* 构建任务持 `GraphUsageGate::Guard`（`QueryExecutor::setUsageGate` 注入）：
  `DROP DATABASE` 的 `waitDrained` 会等它退出；同时 `GraphManager` 仍先 shutdown 构建服务再关连接（双保险）。
* 构建线程不得在 compute/IO 线程上 `blockingWait`（会饿死线程池）；见 §12。

---

## 10. 读路径

* **规划器的两个条件（缺一不可）**：
  1. **事务绑定的元数据版本**里该索引 `state == PUBLIC`（I11：事务期间新转 `PUBLIC` 的索引对旧事务不可见）；
  2. 注册表**当前** `lifecycle == ACTIVE`（`DROPPING`/`PENDING_PURGE`/`PURGING` 一律**不可选**）。
  只有两者都满足才可选。7 处 `idx.state != PUBLIC` 的判定统一收敛为这两个条件（不再各自读 schema 副本）。
* **为什么第 2 条必须看“当前”而不是 pin**：`DROP` 的线性化点是“定义删除 / 置 `DROPPING`”，此后**新计划**不得再选它——
  即使发起计划的事务 pin 里它还是 `PUBLIC`（老事务里的新语句也算新计划）。否则会：
  ① 违反用户语义（“我已经删了，别再拿它算”）；② 在 `PURGING` 窗口里选中一张正在被删的表。
* **执行期读准入与计划期不同**：已在飞的计划（算子已拿到执行权）用 `acquireRead(index_id)` 只要求**数据仍在**
  （`ACTIVE`/`DROPPING`/`PENDING_PURGE` 允许，`PURGING` 拒绝），这样“计划已生成、DROP 插进来”的查询仍能跑完且结果正确；
  数据删除要等它的读句柄释放（§9.3）。
* **算子**：`IndexScanPhysicalOp` / `EdgeIndexScanPhysicalOp` / 复合索引扫描在打开游标前 `acquireRead`；`ReadHandle` 持有期间 DROP 不会删表。释放由 RAII 保证（含取消/异常路径）。
* **`EXPLAIN`**：只展示可用索引；`BUILDING`/`ERROR` 索引不出现。
* **错误语义**：读准入失败 ⇒ 可重试错误（`TxnErrorClass::is_retryable`/查询错误分类沿用 [error-model.md](../query/engine/error-model.md)），由客户端或语句层重试；**不回退到全表扫**（静默降级会掩盖问题，且代价不可控）。

### 10.1 元数据版本与在线 schema 变更（对齐 F1）

**这个问题 F1 已经给过答案**：Google F1 的在线 schema 变更算法（*Online, Asynchronous Schema Change in F1*）
用"元素四态 + 串行 schema 变更 + lease + 异步数据重整"让长事务与 schema 变更共存。我们借用它的骨架，
而不是自己发明一套"版本绑定"。

**F1 规则 → 本设计的落地**

| F1 规则 | 本设计 |
|---|---|
| **schema 是事务元数据**：一个事务只用一个 schema 版本 | `GraphSession::SchemaPin` 在 BEGIN 时持有 `shared_ptr<const GraphSchema>` + `start_generation`/`compat_version`；旧版本由引用计数保活，事务结束释放。**索引可用性与标签/属性可见性都冻结在事务开始**（§10.3） |
| **元素四态**：`absent → delete-only → write-only → public`（添加）；删除反向。每个状态都要与前后状态的数据兼容 | 持久化 `IndexState` 的 `WRITE_ONLY`/`PUBLIC` 对应 **write-only/public**；`absent` = 定义不存在；**delete-only 由运行时的 `DROPPING`/`PENDING_PURGE` + 版本引用表达**（`IndexState.DELETE_ONLY` 是遗留值，v2 不使用）。`ERROR` 是本设计额外加的失败终态 |
| **schema 变更串行、数据重整异步** | 每图一把 `schema_change_mutex`：**定义变更与状态翻转**（毫秒级）串行；builder 的扫描/重放/清理**并行**（受 `index_build_concurrency` 限制） |
| **lease**：旧版本在数据删除前必须仍可用；清理等旧版本事务结束 | 每个 `SchemaPin` 记版本引用（`version_refcount[version]`）；`PURGING` 的清理条件是**包含该索引的旧版本引用归零**（比 `readers==0` 更强：事务可能先拿版本、稍后才扫这个索引） |

**"事务跨越多个版本"：系统允许，但只有两类跨越是安全的**

- **索引可用性冻结在事务开始**：事务记录 `start_generation`；某索引只有在
  `index.public_generation <= txn.start_generation` 时才允许被它的计划选中。原因：中途才可用的索引，
  其条目是在事务数据快照之后才补齐的 ⇒ 只看到一部分条目 ⇒ **静默漏行**。
- **兼容新增不再按语句边界采用**：事务持有固定版本，新标签/新属性对它在事务内不可见；
  只读/自动提交语句各自是新事务，语句开始时取当前版本。以下“单调新增”只用于说明**旧版本为什么仍正确**：
  旧行缺值 = null，新标签没有历史数据，不会让任何已提交数据变"错"。因此事务内的后续语句**应该**看到它们；
  否则"`CREATE (n:NewLabel)` 之后同一事务 `MATCH (n:NewLabel)`"会失败，这是今天的可用行为。
- **不兼容变更会让整个视图冻结**：一旦 `compat_version` 变化（删标签/删属性/改类型/改主键），
  事务退回使用自己开始时的完整快照（破坏性 DDL 会 drain 旧 pin，数据在它结束前不删）。
- 于是"跨越"只发生在事务边界与**单调新增**上；每次跨越都要过下面三条安全条件。

**三条安全条件（每次跨越都要过）**

1. **新版本只在数据完整后发布**：`PUBLIC` 之前，回填数据和 delta 已全部提交（§8.3）。
   ⇒ 看到 `PUBLIC` 的事务，其数据快照一定包含完整索引。
2. **旧版本在数据删除前仍可用**：`DROP` 只改可见性、不立刻删表；物理清理等旧版本引用归零。
   ⇒ 旧事务继续用旧索引，读到的仍是它快照时刻的完整数据。
3. **中间态对两侧都兼容**：`WRITE_ONLY` 期间新老事务都**不读**它；`DELETE_ONLY`（运行时 `DROPPING`）期间新事务不选它、
   旧事务仍可读。⇒ 不存在"一边按新版本写、另一边按旧版本读到半成品"的组合。

**关键的一处：写入维护用"当前注册表"，不用版本**

如果写入也按事务版本走，会有一个 F1 里不存在的洞：**在索引定义创建之前就开始的事务，其版本里没有这个索引**
⇒ 它提交的写入既不进索引也不进 delta ⇒ 构建完成后索引缺条目（旧分支 §20.4 的"新索引对 DML 不可见"就是这个问题）。

本设计的做法：**读计划按版本（`SchemaPin`），写入维护按注册表当前状态**。

- DML 的索引集合来自 `IndexRuntimeRegistry`（按 label 查），而不是语句持有的 `label_defs` 快照；
  因此**任何事务**（包括版本里还没有该索引的旧事务）在构建期都会把写入落到该索引的 delta 里——
  注册表缓存了创建时解析好的 `prop_ids`/accessor 结果，旧版本事务也能算出条目。
- 写者若在版本里看到 `WRITE_ONLY` ⇒ 准入写 delta；若期间翻 `PUBLIC` ⇒ 准入失败 ⇒ 重试时按注册表看到 `PUBLIC` ⇒ 直写索引。
- 写者若版本说 `PUBLIC` 而注册表已 `DROPPING` ⇒ 仍直写索引（数据保留到旧版本 drain）。
- 三种交叉都由**准入 + 版本引用**保证安全，所以**只有读计划需要版本绑定**。

这也是我们与 F1 的偏离点：F1 是分布式系统，写入必须按 schema 版本走，所以它用 **lease + "先等旧事务退出再回填"**
来消除"旧事务不维护新元素"的洞；我们是单进程、有共享注册表，可以直接让写入维护跟随当前状态，
因此**不需要让构建等待旧事务**（回填可以立即开始）。代价是必须实现 §6 的准入闸门（F1 用 lease 达到同一目的）。

**实现要点**

- `GraphSchema` 版本化：`std::shared_ptr<const GraphSchema>`；DDL 生成新版本并递增 `schema_generation`；
  事务在 BEGIN 时 pin 一个版本（`GraphSession::SchemaPin::schema`），旧版本由引用计数保活；
- `GraphSession` 持有 `SchemaPin{shared_ptr, version}`：`BEGIN`（或自动提交语句开始）时获取，`commit/rollback/close` 释放；
- `version_refcount[version]`：`PURGING` 清理的条件是"该版本引用归零"，超时 ⇒ `PENDING_PURGE`（绝不强删）；
- 规划器从 `SchemaPin` 取索引定义与 `IndexState`；写路由读注册表；`SHOW INDEXES` 读**当前**快照 + 注册表（观测面不受绑定限制）；
- **已定：显式事务内执行 DDL 一律拒绝**（`DDL cannot run inside an explicit transaction`）。理由：meta 写在独立存储/连接上，无法与数据事务一起回滚；允许它会出现"事务回滚了、索引还在"的隐藏语义，且在版本绑定下该语句对自己的事务不可见。自动提交语句不受影响。

**后果（必须写进测试）**

| 场景 | 行为 |
|---|---|
| 索引在事务期间转 `PUBLIC` | 该事务**读计划不用**它（全表扫/其它索引），结果正确；**下一个事务**才用 |
| 索引在事务期间被 `DROP` | 该事务继续用（版本里有它，数据保留到版本 drain），清理后台等它结束 |
| 旧事务（版本里没有新索引）在构建期写入 | **仍维护新索引**（写 delta）；构建完成后索引完整 |
| 事务内执行 DDL（索引/标签） | **报错** `DDL cannot run inside an explicit transaction`（已定）；自动提交不受影响 |
| 事务内 `SHOW INDEXES` | 读当前状态，不受版本绑定限制 |

**写事务提交**：见 §10.2（按 touched 元素的提交校验与破坏性 DDL 的 drain 规则）。

**与今天的行为差异**：今天每条语句都会重新拷贝 `labels`/`edge_labels`（`QueryExecutor` 在语句开始时填充 `StreamContext`），
所以多语句事务**已经在跨版本**、且是"逐语句版本"。§10.1 把它收敛为"事务版本"，并补上写入维护的注册表路由，
消除"新索引上线 + 旧数据快照"的漏行与"旧事务漏维护"两个洞。实现落在 P1b。

### 10.2 schema 版本与提交校验（I12）

§10.1 解决"**读计划**用哪个版本"，这一节解决"**写事务提交**能不能通过"。

**三个计数器（各司其职，不要混用）**

| 名字 | 何时 +1 | 用途 | 会不会导致事务失败 |
|---|---|---|---|
| `schema_generation` | **每次定义发布**：自动建标签、`__anon__` 加属性、显式建标签/加属性、索引定义增删、`PUBLIC` 翻转 | 快照识别、pin/drain 计数、观测 | 否（单独不构成失败） |
| `compat_version` | **只在不兼容变更**：删标签/边标签、删属性、改属性类型、改主键 | 提交校验快路径 + 触发按元素校验 | 只有 touched 元素被删/不兼容时 |
| `index.public_generation` | 该索引转 `PUBLIC` 时，记录当时的 `schema_generation` | 计划期判断"这个索引对这个事务可用吗" | 否 |

* 旧图没有这些键 ⇒ 0，不需要数据迁移；都持久化到 meta，重启后单调（缺省 0）。
* **兼容新增不递增 `compat_version`**：自动建标签与 `__anon__` 加属性是运行时高频操作
  （`CreateNodePhysicalOp` 的 Phase 0 自动建标签、`getOrCreateAnonPropId` 追加弱属性），
  让它们递增兼容版本只会让长事务一直走慢路径，没有任何收益。
* `schema_generation` 会因这些隐式新增持续增长，但**它只用于记账**，不参与"是否冲突"的判定。
* 实现上，`PUBLIC` 翻转是否发布新快照都可以（状态在注册表里也有一份）；若发布，`public_generation` 取当时值。

**事务的 touched 集合**

* `GraphSession` 记录本事务**写到的 schema 元素**：`(label_id | edge_label_id, 可选 prop_id)`，以及准入过的 `index_id`；
* 去重、只增；一条语句通常 1–3 个元素，成本可忽略；
* 只读事务不记录、不校验。

**提交校验：按元素判定（A 方案）**

```text
commit():
  if !session.wroteAnything():                      # 只读事务
      return doCommit()
  pin = session.schemaPin()
  if pin.compat_version == meta.currentSchema().compat_version:
      return doCommit()                              # 快路径：没有不兼容变更
  cur = meta.currentSchema()
  for e in session.touched():                        # 有版本变化：只看本事务写到的元素
      if !compatible(cur, e):
          return Abort(SchemaChanged)                # 可重试；绝不提交
  return doCommit()

compatible(cur, e):
  label / edge_label : 仍存在，且**已有属性**的 id 与类型未变、未被删除；**追加新属性不算冲突**
                       （旧事务不写它，语义等价于 NULL）
  property           : 仍在对应 label 定义中（未被删除/改类型）
  index              : 恒为 true（索引增删不构成提交冲突，由 §6 准入与 §9.3 drain 处理）
```

* **为什么不是"落后超过 N 版就 abort"**：版本距离只是代理指标——无关的索引定义变更会把长事务误杀，
  而一次版本里批量改多个元素时"落后 1 版"也可能不安全。按元素判定既精确又不会误杀。
* 只读事务永不因版本被拒（它读到的就是自己 pin 的版本，结果自洽）。
* **自动提交语句**同样按语句开始时绑定的 pin 校验；提交失败重试时沿用同一 pin，重试前再校验一次。

**原子性与锁序**

* 版本发布：`schema_change_mutex` **排它**，只做"换快照 + `++version`"（毫秒级）；
* 提交校验：`schema_change_mutex` **共享**，覆盖"读当前版本 + 校验 + WT commit"，避免"校验通过后 DDL 插入再提交"；
* **drain 等待不在锁内**：破坏性 DDL 先发布 delete-only 版本、**放锁**、等 `min_alive_version >= drop_version`、
  再取锁做物理删除（否则会卡住旧事务的提交，形成死锁）。

**破坏性 DDL 的两阶段（F1 lease 的确定性版本）**

| 阶段 | 动作 | 读者 / 写者 |
|---|---|---|
| delete-only | 发布新版本：元素从**新计划/新写入**消失；旧 pin 仍能看到它 | 旧事务照常读；新事务不再使用 |
| drain | 等所有 `< drop_version` 的 pin 释放（`min_alive_version`） | 旧事务结束前不会进入下一阶段 |
| purge | 排它锁内物理删除数据/定义；失败 ⇒ 后台重试（幂等） | — |

**为什么不照搬 F1 的时间租约**

F1 是分布式系统：schema 要传播到多台服务器、事务可能在任何一台提交，所以只能用**时间租约 + 写防护**
保证"最多两个版本共存"；代价是长事务超过租约就被拒绝，DDL 也必须按租约周期一步步推进。

我们是**单进程**：所有事务的 `SchemaPin` 都在同一份内存里，可以**精确计数**（引用数 + `min_alive_version`），
不需要猜时间。保留 F1 的**语义**（相邻态兼容、写入防护、DDL 不被长事务永久阻塞），去掉**时间假设**：

| 维度 | F1 | 本设计 |
|---|---|---|
| 旧版本何时失效 | 租约到期（时间） | 最后一个 pin 释放（计数） |
| DDL 推进速度 | 每步至少一个租约周期 | 立即推进到 delete-only，只等真正在飞的事务 |
| 长事务 | 超过租约 ⇒ 提交被拒 | 只要它写到的元素未被删就能提交；破坏性 DDL 等它 |
| 失败来源 | 时间（不可预测） | 只有真实冲突（元素被删/不兼容） |

**失败与重试**：`SchemaChanged` 归为可重试错误；客户端/语句层用新的 `SchemaPin` 重新计划后重试。
指标：`schema_version`、`schema_pin_oldest_age_ms`、`schema_conflict_aborts_total`、`schema_drain_wait_ms`。

**持久化与单调性**：`schema_version` 与定义变更在同一次 meta 写里落盘（或紧随其后）。崩溃导致版本号落后不会破坏正确性
（重启后没有在飞 pin），只需启动时保证单调（取持久化值，缺省 0）。**时间租约只在多实例共享同一图时才需要**——
本设计的计数方案以单进程为前提；若将来多实例部署，必须补 F1 式的租约或跨实例版本协调。

**与唯一索引的关系**：写入的唯一性检查按**注册表当前状态**（不按版本）。否则旧版本事务在唯一索引已 `PUBLIC`
后写入重复键，就会绕过约束、把重复带进已上线的索引（§8.4）。


### 10.3 DDL 兼容性分类与影响范围

**前提（当前代码已成立）**：属性是**可选**的，按 `{vid}{0x01}{label}{prop_id}` 存在 `vertex_data`
（[kv-encoding.md](kv-encoding.md)），缺 key = null；`prop_id` 在每个 label 内**只追加、永不复用**。
所以"加一列"不需要改任何既有数据行。

**两类"隐式 DDL"要特别点出**（运行时高频，且在写路径上）：

* `CREATE (n:NewLabel …)` ⇒ `CreateNodePhysicalOp::prepareLabels_` **自动建标签**（`createOrGetLabel_`）；
* 未声明属性 ⇒ 走弱空间 `__anon__`，`getOrCreateAnonPropId` **追加一个 `ANY` 属性**（不扩到用户标签的定义上，见 #315）。

两者都是**兼容新增**。

**分类表**

| 操作 | 数据/编码兼容 | `compat_version` | `schema_generation` | 影响哪些事务 | 失败形态 |
|---|---|---|---|---|---|
| 运行时自动建标签 | 是（新 id/新表，无历史数据） | 不变 | +1 | 无提交失败；旧视图按自己的定义解析 | 无 |
| `__anon__` 追加属性 | 是（旧行无 key = null） | 不变 | +1 | 无 | 无 |
| 显式 `createLabel`/`createEdgeLabel`（新标签） | 是 | 不变 | +1 | 无 | 无 |
| `createLabel(merge_properties)` 追加可选属性 | 是 | 不变 | +1 | 无 | 无 |
| `CREATE INDEX`（定义新增，`BUILDING`） | 是（新表；写入走 delta；读不用） | 不变 | +1 | 无 | 无 |
| 索引 `BUILDING → PUBLIC` | 是 | 不变 | +1 | **事务开始后才 PUBLIC 的索引对该事务不可见**；新事务可用 | 无 |
| `DROP INDEX` | 是（数据保留到 drain） | 不变 | +1 | 新计划立刻不选；在飞查询跑完 | 无 |
| **删属性** | **否**（旧写入的 prop id 变废弃；若被索引引用则索引语义失效） | **+1** | +1 | touched 含该 `(label, prop)` 的**写**事务；只读事务仍可读（drain 前） | 提交 `SchemaChanged`（可重试） |
| **删标签/边标签** | **否**（旧写入会落进已删元素） | **+1** | +1 | touched 含该 label 的写事务 | 同上 |
| **改属性类型** | **否**（旧写入类型不符） | **+1** | +1 | touched 含该属性的写事务 | 同上 |
| **改主键定义** | **否**（唯一/端点解析语义变化） | **+1** | +1 | touched 含该 label 的写事务 | 提交失败 / loader 报错 |
| 统计信息、孤儿记录、`IndexState` 之外的运行时元数据 | 是 | 不变 | 视是否发布快照 | 无 | 无 |

**影响范围的统一规则**

1. **只读事务永不因 schema 冲突失败**：它读自己的视图 + 数据快照；被删元素的数据保留到 drain。
2. **写事务只在自己 touched 的元素被删/不兼容时失败**（§10.2 的按元素校验）；无关 DDL 不失败。
3. **索引 DDL 永不导致提交失败**：新增/删除/上线只影响"计划能否选它"和"数据何时清理"。
4. **隐式新增永不失败**：只影响"谁看得见"——事务看不到 BEGIN 之后发布的兼容新增（固定 schema 版本）；
   自动提交语句各自是新事务，能看到语句开始时的版本；索引可用性仍按 `public_generation` 冻结。
5. **语句错误 ≠ 提交失败**：引用自己视图里不存在的标签/属性是**语句错误**（`Label not found` / `Property not found`）；
   `SchemaChanged` 只在"我写过的元素被删/不兼容"时出现。

**实现注意（写热路径）**

* `__anon__` 属性追加在写热路径上：每次新增名字都会 `publishSchemaVersion()` 深拷贝整个 `GraphSchema`，
  名字很多时会拖慢写。当前先按正确性实现；后续优化为**单独的 COW 子快照**
  （`shared_ptr<const AnonPropMap>` + 原子 size），发布时只换这个指针，`LabelDef(__anon__)` 读时查子快照。
* 自动建标签是"每标签一次"，全量快照 copy 可接受；不要在此时复制表/数据。
* `schema_change_mutex` 只保护"换快照 + 计数器"，**不得覆盖 `persistLabelDef` 的 IO**；持久化失败要回滚内存快照（现状如此），且不能发布半成品。

**与索引构建的关系**

* builder **不能**用 `schema_generation` 是否变化来决定失败——隐式建标签会让它一直变。
  只检查它依赖的元素：被索引的标签/被索引的属性被删或改类型、索引定义变化 ⇒ `ERROR`（§11.3）。
* 追加属性不影响已有索引（prop id 只追加，索引定义引用既有 id）；后续写入自然带/不带该值。

### 10.4 边界与兼容性清单（实现前逐条对照）

标记：✅ 已定/按现状；❓ 待拍板或需实现验证。

**A. 事务与版本生命周期**

| # | 事项 | 结论/处理 |
|---|---|---|
| A1 | `SchemaPin` 绑定时机 | ✅ 必须在 **BEGIN 之后、任何数据访问之前**；自动提交语句在计划前。服务层挂点是 `graph_service.cpp` 的 `begin_write()`（当前 408/511 两处） |
| A2 | pin 与准入守卫的释放 | ✅ `GraphSession::commit/rollback/close` 统一释放；**会话归还池时必须清空**，防复用串用（§6.3） |
| A3 | `SchemaChanged` 的作用域 | ❓ 建议**显式事务整体失败**（客户端重试整个事务），自动提交语句可重试；不能"只重试语句、沿用旧 pin"（会再失败） |
| A4 | 显式事务内 DDL 的错误级别 | ❓ 建议**语句级失败**（事务可继续/回滚），不是自动 abort；需与错误模型对齐 |
| A5 | 隐式 schema 变更是否随事务回滚 | ✅ **不随**：建标签/`__anon__` 加属性是单调新增，rollback 后仍存在；文档与测试要写明（用户不应期待回滚撤销标签） |
| A6 | 同一事务内兼容新增的可见性 | ✅ 事务级不可变 schema：不可见；自动提交语句各自新事务 |
| A7 | 计划已生成、算子未开游标时 DROP | ✅ 事务的版本 pin 会阻止 `PURGING`；算子 `acquireRead` 允许 `DROPPING`；测试覆盖 |
| A8 | `public_generation` 记录时机 | ✅ 必须在**最后一批数据提交之后、对外可见之前**记录，否则旧事务可能用到刚 `PUBLIC` 的索引 |
| A9 | 计数器持久化/重启单调 | ✅ 与定义同批写；丢失 bump 不影响正确性（重启无旧 pin），启动后单调即可 |
| A10 | 多图隔离 | ✅ 计数器、注册表、`schema_change_mutex`、drain 全部按图一份 |

**B. 隐式 DDL 与并发**

| # | 事项 | 结论/处理 |
|---|---|---|
| B1 | 并发自动建同名标签 | ❓ 需要验证 `createOrGetLabel_` + `createLabel` 在竞争下返回**同一个 id**（不是报 duplicate），且 `store.createLabel(lid)` 幂等 |
| B2 | `__anon__` 属性并发分配 | ✅ 现状有 `anon_prop_mu_` + 持久化失败回滚；补并发测试（分配不重号、不丢） |
| B3 | `__anon__` 属性名爆炸 | ❓ 属性名是用户输入，长期运行会累积元数据；需要**上限/告警策略**（拒绝新名字？告警？容量监控？） |
| B4 | 隐式 DDL 的发布成本 | ✅ `__anon__` 用独立 COW 子快照；自动建标签一次性；**不得在写路径持 `schema_change_mutex` 做 IO**；用 p99 验证 |
| B5 | builder 与隐式 DDL | ✅ 元素级检查（§11.3）；补"构建中大量建新标签不误杀/不拖慢"的测试 |

**C. 索引与唯一性**

| # | 事项 | 结论/处理 |
|---|---|---|
| C1 | 唯一约束对旧版本事务的生效边界 | ✅ 写入按注册表当前状态查重：索引 `PUBLIC` 后，旧事务的重复写入**会被拒**；语义要写进 API 文档 |
| C2 | 构建期重复导致 `ERROR` 后的恢复 | ✅ `DROP INDEX` → 清理重复 → 重建；文档写明 |
| C3 | `encoding_version != 2` 的索引 | ✅ 规划器忽略（现状）；新建索引必须置 2 |
| C4 | 旧事务看不到新索引 / 新事务看到 `BUILDING` | ✅ 由 pin + registry 决定；`SHOW INDEXES` 读当前 |
| C5 | DROP 后同名重建 | ✅ 新 id / 新表；旧 pin 仍指向旧表（id 不复用），drain 后才删 |

**D. 客户端与兼容面**

| # | 事项 | 结论/处理 |
|---|---|---|
| D1 | DDL 返回文案/时机变化 | ✅ 已知破坏面；loader/测试/脚本迁移（§13.2） |
| D2 | `SHOW INDEXES` / `db.indexes` 词表 | ✅ 已定（BUILDING vs Neo4j 词表） |
| D3 | `SchemaChanged` / `DDL cannot run…` 的错误码与 Bolt 映射 | ❓ 需要映射到可重试错误（Neo4j `TransientError` 或查询错误分类），并决定客户端是否自动重试 |
| D4 | 客户端重试策略 | ❓ 建议引擎**不自动重试**（显式失败、可观测），由 SDK/loader 决定 |
| D5 | 老图兼容 | ✅ 无版本键 ⇒ 0；`index_id==0` 边索引启动置 `ERROR`；`DELETE_ONLY` 按 `ERROR` 处理 |
| D6 | 多进程/滚动升级 | ❓ 单进程模型下暂不考虑；多实例必须补租约（§10.2 末） |

**E. 生命周期与崩溃**

| # | 事项 | 结论/处理 |
|---|---|---|
| E1 | 崩溃点覆盖 | ✅ CREATE 定义后/建表后/SCAN/CATCHUP/FINALIZE/落 `PUBLIC` 前/DROP 中，各有启动策略（§9.1/§9.3）；测试逐点覆盖 |
| E2 | 关图/关进程 | ✅ 先 `shutdown()` 再关连接；构建持 `GraphUsageGate` |
| E3 | **drain 无上限 vs 超时强制** | ❓ 破坏性 DDL 等旧 pin：正确性优先 = 无上限；可用性 = 超时后如何处置。建议默认无上限 + 可配置超时（超时转"等待中/告警"，**不强制删**），强制仅运维显式 |
| E4 | 会话池耗尽 | ✅ DDL/构建退避重试；不阻塞查询 |
| E5 | 长 DDL 期间的 checkpoint/IO | ❓ 观测项（P4）：drain 窗口与 checkpoint 重叠时的写延迟 |

**F. 性能与观测**

| # | 事项 | 结论/处理 |
|---|---|---|
| F1 | pin/version 计数的并发开销 | ❓ `min_alive_generation` 的维护方案（版本计数 map + 最小值；或分片近似）；避免每事务全局锁 |
| F2 | touched 集合成本 | ✅ 每事务 1–3 个元素，去重后记录 |
| F3 | 指标/日志 | ✅ `schema_conflict_aborts_total`、`schema_drain_wait_ms`、`schema_generation`、`compat_version`、`index_build_*`；隐式 DDL 日志**采样**，避免刷日志 |
| F4 | schema 快照大小/copy 频率 | ✅ 用 A/B 测；`__anon__` 走子快照（B4） |

**拍板（本轮，关闭上表的 ❓）**

| # | 问题 | 决定 | 理由 |
|---|---|---|---|
| 1 | `SchemaChanged` 作用域（A3） | **显式事务整体失败**；自动提交语句返回可重试错误，由客户端重试 | 事务在旧视图上继续做后续工作没有意义，且容易让调用方误以为还能提交；整体失败语义简单、可观测 |
| 2 | 显式事务内 DDL 的错误级别（A4） | **语句级失败**，事务仍可继续/回滚 | DDL 被拒绝、什么都没发生，继续事务是安全的；不制造"莫名 abort" |
| 3 | 同一事务内兼容新增可见性（A6） | **不可见**（事务级不可变 schema 版本） | 与"每个事务一个 schema 指针"一致，避免语句间 schema 漂移；显式事务内 DDL 本就被拒绝，不存在"CREATE 新标签 → 同事务 MATCH"路径；自动提交语句各自新事务，能看到最新版本 |
| 4 | drain 是否有上限（E3） | **默认无上限等待**（DDL 异步返回，后台等）；只做 `schema_drain_warn_ms` 告警，**不强制删** | 单进程可精确计数；正确性优先；强制推进会让旧事务在提交时才失败，行为更差 |
| 5 | `__anon__` 属性增长（B3） | **软上限 + 告警**（默认 100k 个不同名/图，可配），**不拒绝**新名字；指标暴露增长速率 | 隐式列是正常 ingestion 行为，硬拒绝会破坏导入；但要能被观测到 |
| 6 | `SchemaChanged` / DDL-in-txn 的错误映射（D3） | `SchemaChanged` 归为**可重试**（Bolt `TransientError` 或查询错误分类）；`DDL cannot run inside an explicit transaction` 为**不可重试**语句错误 | 驱动/客户端需要能区分"重试有用"与"重试无用" |
| 7 | 引擎是否自动重试（D4） | **不自动重试**：显式失败，交给 SDK/loader/客户端 | 自动重试会掩盖 schema 变更频率，也让超时/取消语义复杂 |

## 11. 并发分析

### 11.1 参与方与等待关系

| 参与方 | 等待什么 | 持有什么 |
|---|---|---|
| DML 写者 | 准入（`tryEnter`，有界） | 会话事务 + 准入守卫 |
| builder（SCAN/CATCHUP） | 无（只写自己的索引表/变更表） | 自己的 `SessionLease` |
| builder（FINALIZE） | `inflight == 0`（有界） | 闸门（CLOSING） |
| `DROP INDEX` | builder 退出、**引用该索引的旧版本归零**（有界） | 生命周期迁移 |
| 读者 | 无（准入失败即返回可重试错误） | 读句柄（refcount） |
| 关图 | builder 退出 | 图级闸门 |

**无环**：唯一的"等待链"是 `FINALIZE 等写者` 与 `DROP 等读者`；两者都不持有对方需要的资源（builder 不持读者句柄；读者不持准入守卫；写者不等 builder）。所有等待都**有超时**且超时路径是"回退 + 告警"，不制造无限等待。

**锁序与收尾串行化**：一次写入可能触及多个索引 ⇒ 按 **index_id 升序**逐个准入（收集完条目后先排序再准入），禁止在执行深处边写边取。
**每图同一时刻最多一个索引处于 `FINALIZE`（CLOSING）**：`SCAN`/`CATCHUP` 可以按 `index_build_concurrency` 并行，但最终化阶段由 `IndexBuildService` 串行。这样"写者持有索引 C 的准入、又在等索引 A 的闸门"时，A 的收尾者只等 A 的 in-flight（不含该写者），不会形成等待环；若并发收尾两个索引，写者可能同时被两道闸门阻挡，导致语句反复超时失败。

### 11.2 危险清单（v2 视角）

| # | 症状 | 机制 | 规则 |
|---|---|---|---|
| H1 | 构建期写入丢失 | 写者读到 `BUILDING` 后状态翻到 `FINALIZE`，写入落在排空之后 | I5 准入 + 关闸；写者等待而非直写 |
| H2 | 写入"写了但没提交"仍被算作已排空 | 准入按"写调用"而不是"事务"计数 | I6：守卫绑定 `GraphSession` |
| H3 | 排空被长事务拖死 | 长用户事务在 FINALIZE 时仍持有守卫 | 前置条件②（最老 inflight 年龄）+ 超时回退 CATCHUP |
| H4 | `PUBLIC` 但索引不全 | 先翻状态后排空；重放失败被忽略 | §8.3 顺序 + 校验失败即 ERROR |
| H5 | 构建期唯一索引出现重复 | 并发写入互相不可见 | 构建期不强制；最终态两段校验；`PUBLIC` 后强制 |
| H6 | DROP 让在飞查询读到半删状态 | 无读准入 | §9.3 读句柄 + DROPPING 期间继续维护 |
| H7 | 关图/`DROP DATABASE` 与构建并发 ⇒ 关闭中的连接被使用 | 无生命周期保护 | §9.4：先 shutdown 再关 store + 构建持 `GraphLease` |
| H8 | 重建后残留旧条目 | ERROR 时未清理部分索引表 | §7.3/§9.1 强制 drop |
| H9 | 多图同号索引互相干扰 | 进程级闸门/注册表 | I8：每图注册表 |
| H10 | 后台构建与查询共用会话 ⇒ WT 会话损坏 | 共享 txn/session | I10：builder 用自己的租约 |
| H11 | schema DDL 与构建并发 ⇒ 构建按过期 `(label, prop)` 解析 | 无版本校验 | §11.3 版本校验 ⇒ 构建 ERROR |
| H12 | 会话池耗尽 | 构建/查询抢会话 | 获取失败 ⇒ 退避重试；构建最多占 2 个会话 |
| H13 | builder 重放与用户事务写冲突 | WT 乐观并发 | 短批次 + 整事务重试 + 指标；必要时切 §19.2 备选 |
| H14 | 空索引上线 | 回填无产物仍置 PUBLIC | §8.5 护栏 |
| H15 | `index_id` 复用导致删错表 | 复用 id | 已由"每图单调递增、不复用"保证；注册表拒绝 0 |

### 11.3 模式 DDL 与构建

* **schema 变更串行**（F1 规则）：`schema_change_mutex` 只保护"定义变更 + 状态翻转"（毫秒级）；
  builder 的扫描/重放/清理**不持锁**、可并行。
* 构建开始时记录自己的 `SchemaPin` 与依赖元素（标签/边标签定义、被索引的 `prop_ids`、索引定义）；
* 每个批次边界做**元素级**复查：标签/边标签被删、被索引的属性被删或改类型、索引定义变化 ⇒ `ERROR`
  （`"schema changed during index build"`）。**新增标签、追加属性、别的索引增删/上线都不中止构建**——
  否则 DML 的隐式建标签会不断误杀长构建。
* 只做元素级校验、不持长锁：`createLabel`/`dropLabel` 不会因为构建跑几分钟而被阻塞。
* `createLabel`/`dropLabel` 自身仍按既有路径加锁；索引定义变更与它们互斥由上面这把 `schema_change_mutex` 保证。

---

## 12. 执行器与后台服务

声明顺序（C++ 成员按**逆序**析构，因此这里列出的是"先声明 ⇒ 后销毁"）：

```text
GraphInstance
 ├─ usage / data_conn / meta_conn / sync_* / async_session / async_data / async_meta   （既有）
 ├─ session_pool            （既有：必须晚于 store 销毁 ⇒ 最后销毁的一批）
 ├─ index_registry          （新：executor 与 builder 都引用它 ⇒ 必须晚于两者销毁）
 ├─ executor                （既有）
 └─ index_builds            （新：最后声明 ⇒ 最先销毁 ⇒ 先排空构建，再让 executor/registry/store 退出）
```

约束：
* `index_registry` 必须先于 `executor` 声明（executor 持裸指针）；
* `index_builds` 声明在 `executor` 之后是**防御性约定**；实际安全由 `GraphInstance` 析构函数与
  `closeAll`/`dropGraph` 显式 `index_builds->shutdown()` 保证（析构体先于成员析构执行），
  不能只依赖声明顺序。

**模块划分与依赖方向（可测试性优先）**

* 依赖方向：`common/types ← storage/index ← storage/data ← query/graph/service`；**禁止反向**；
* 决策与执行分离：`index_write_router`（纯判定）vs `index_write_applier`（执行，协程）；
* 索引条目**只有一份定义**：`storage/index/index_entry.hpp`，compute 层用 `using` 别名；
* 会话不依赖索引特性：准入资源通过 `GraphSession::onTransactionEnd()` 通用回调挂载；
* data 层只保留两个窄端口（`indexRegistry()`/`graphSession()`），不实现索引语义；
* 纯逻辑 header-only；构建器/服务/孤儿日志等有 IO/线程的模块用 `.hpp/.cpp`。

**文件清单（当前）**

| 文件 | 层 | 职责 | 测试 |
|---|---|---|---|
| `common/types/index_runtime.hpp` | common | Phase/Lifecycle 状态机、`plannerSelectable`、写去向/唯一性判定 | `index_runtime_tests` |
| `common/schema/schema_compat.hpp` | common | 提交期兼容性纯判定（未接线） | `index_runtime_tests` |
| `storage/index/index_runtime_registry.hpp` | storage/index | `IndexRuntime`、两阶段准入、读句柄、每图注册表 | `index_runtime_tests` |
| `storage/index/index_runtime_factory.hpp` | storage/index | 定义 → 运行时描述（表名解析） | `index_runtime_tests` |
| `storage/index/index_entry.hpp` | storage/index | 顶点/边索引条目唯一定义 | 编译期 |
| `storage/index/index_write_router.hpp` | storage/index | 写决策：去向/表名/准入/查重 | `index_write_router_tests` |
| `storage/index/index_write_types.hpp` | storage/index | 条目/状态（异步+同步共用） | 编译期 |
| `storage/index/index_write_applier.hpp` | storage/index | 异步执行决策 + 事务准入绑定；legacy 回退 | `index_write_applier_tests` / e2e |
| `storage/index/index_write_applier_sync.hpp` | storage/index | 批量装载的同步执行（FINALIZE 返回 RetryLater） | `index_write_applier_tests` |
| `storage/index/index_builder.{hpp,cpp}` | storage/index | SCAN（顶点/边）+ FINALIZE：回填→关闸→排空→追平→空索引/唯一性校验 | `index_e2e_tests`（含 BuilderRunsWithoutQueryExecutor） |
| `storage/index/index_build_service.{hpp,cpp}` | storage/index | 每图构建服务：FIFO/线程/取消/shutdown/周期任务 | `index_build_service_tests` |
| `storage/index/index_delta_codec.hpp` | storage/index | delta `{op,payload}` 编解码 | `index_store_tests` |
| `storage/index/index_test_hooks.hpp` | storage/index | 命名暂停点（默认 no-op） | `index_runtime_tests` |
| `storage/index/index_maintenance.hpp` | storage/index | 批量路径条目收集 + sync applier 调用 | `index_write_applier_tests` |
| `storage/session/graph_session.hpp` | storage/session | `onTransactionEnd`（结束执行）、`SchemaPin` | session/e2e |
| `storage/session/schema_generation_tracker.hpp` | storage/session | schema 代数 pin 计数（DROP 等旧事务） | `index_runtime_tests` / e2e |
| `storage/meta/async_graph_meta_store.*` | storage/meta | `schema_generation`/`compat_version`、清理记录、启动恢复 | e2e / restart |
| `storage/data/{i_,}async_graph_data_store.hpp` | storage/data | 语句会话门面；registry/graphSession 窄端口；delta 包装 | `index_store_tests` |
| `graph/graph_manager.{hpp,cpp}` | graph | 注册表/构建服务装配；打开恢复；关图顺序 | `catalog_tests` / restart |
| `query/executor/query_executor.*` | query | DDL 入口、构建编排、发布/失败/删除、SHOW INDEXES | `index_e2e_tests` |
| `query/physical_plan/operator/index_scan*` / `edge_index_scan*` | query | 计划执行 + 读句柄 | `query_executor_tests` |
| `program/loader/csv_loader.cpp` | loader | 异步 DDL 等待方（轮询 SHOW INDEXES） | loader 集成 |

**命名与"新增代码放哪"**

| 要加什么 | 放哪 | 不要做什么 |
|---|---|---|
| 新 DML 写路径 | `IndexWriteItem` + `applyIndexWrite` | 自己判断 state/表名/unique |
| 新索引 DDL | `QueryExecutor::handleIndexDdl` + registry 生命周期 | 把构建逻辑塞进 router/applier |
| 构建阶段/追赶策略 | `index_builder`（需先抽 SCAN/CATCHUP） | 改 router/applier |
| 可观测字段 | `IndexRuntime` 原子字段 + SHOW 读取 | 改写路径 |
| 新错误分类 | `IndexWriteStatus` + 调用方映射 | 底层静默跳过 |
| 提交校验 | `common/schema/schema_compat.hpp` + 提交路径接线 | 塞进 registry |
| 孤儿/待清理 | `storage/meta` 清理记录 API | 散落在 executor/graph_manager |

命名：纯逻辑/策略 header-only；带 IO/线程/持久化副作用的 `.hpp/.cpp`；辅助细节放 `*_detail`。

**as-built 偏差（重要）**

* 构建已抽成 `storage/index/index_builder.{hpp,cpp}`：`runIndexBuild` 负责 SCAN + FINALIZE，
  只依赖 `AsyncGraphDataStore`/`SessionPool`/`IndexRuntimeRegistry`，不依赖 QueryExecutor；
  `QueryExecutor` 只做 DDL 编排与 meta 状态落库，后台服务直接投递 builder（不再有 `on_ready` 握手）。
  编译上放在 `eugraph_index` target（依赖 Folly coro + eugraph_lib）。
* 构建持 `GraphUsageGate::Guard`；关图保护同时来自"先 shutdown 构建服务再关连接"（双保险）。
* 孤儿/待清理记录在 `AsyncGraphMetaStore`（`M|cleanup:*`），没有独立的 `index_orphan_log` 文件。
* `db.indexes.populationPercent` 仍只按 state 给 0/100，未接运行时 progress。

**schema 并发（版本化 shared_ptr，已实现）**：`AsyncGraphMetaStore` 维护
`published_schema_ = shared_ptr<const GraphSchema>`；DDL 写者在 `schema_write_mu_` 下修改工作副本
`schema_`，持久化成功后 `publishSchemaVersion()` 发布新版本。读者用 `schemaPtr()` O(1) 取当前版本，
事务在 `SchemaPin` 里持有到结束；旧版本由 `shared_ptr` 引用计数释放。
`schemaSnapshot()` 只保留给测试/一次性用途，不再用于热路径。

**语句/计划不再复制 label 定义**：`StreamContext` 持有 `shared_ptr<const GraphSchema>`，
`label_defs`/`edge_label_defs` 是对该版本 map 的引用；`Catalog` 与扫描算子也持有同一个版本指针。
写语句需要 DML 隐式建标签/属性，因此在该语句内持有一个本地可变副本（`owned_schema`），
只在语句内可见；`schema()` 接口已删除，统一用 `schemaPtr()`/`schemaSnapshot()`。

**接线点（不放在这些模块里）**：

* `GraphInstance`：持有 `IndexRuntimeRegistry` 与 `IndexBuildService`（声明顺序见 §9.4）；
* `GraphSession`：持有 `SchemaPin` 与准入守卫集合，在 commit/rollback/close 统一释放；
* DML 算子：只调 `IndexWriteRouter`，不再自己判断 `state`；
* `QueryExecutor`：DDL 建定义/表/运行时、解析 spec、投递/等待 builder、落 meta 状态；
* `graph_manager`：图打开时的启动恢复与注册表重建。

**`IndexBuildService`（每图一个）**

* FIFO 队列 + 并发度 `index_build_concurrency`（默认 2，1 = 串行）；同索引重复提交幂等（已在跑/排队 ⇒ 复用）。
* 工作线程：每图 `concurrency` 个专用 `std::thread`（或一个固定线程池），每个线程执行 `folly::coro::blockingWait(builder.run(index_id))`；**禁止在 compute/IO 线程上 blockingWait**。
* builder 协程内部：
  * 通过 `SessionPool::acquire()` 拿自己的 `SessionLease`（每批 begin/commit；拿不到 ⇒ 退避重试）；
  * 扫描/重放/校验都用这个会话；
  * 每阶段边界检查取消令牌（`DROP`/关图）。
* `shutdown()`：置取消 → 唤醒队首 → join 所有工作线程 → 返回；由 `closeAll`/`dropGraph` 在关 store 前调用。
* 测试可注入同步 runner（`submit()` 内直接执行完），使状态断言确定、不依赖时间。

**DDL 与观测流程（异步）**

| 语句 | 同步部分（语句内完成） | 异步部分（后台） | 返回 |
|---|---|---|---|
| `CREATE INDEX` | 解析/校验、建索引定义（`WRITE_ONLY`）、建索引表与变更表、注册 `IndexRuntime(phase=SCAN)`、`submit` | SCAN → CATCHUP → FINALIZE → 落 `PUBLIC`/`ERROR` | `Index created (building): <name>` |
| `DROP INDEX` | 标记 `DROPPING`、持久化 `pending_drop`、删定义、返回 | cancel builder → 等旧版本引用归零 → 删表 → 清记录（§9.3） | `Index dropped (dropping): <name>` |
| `SHOW INDEXES` | 读当前 schema + 注册表快照 | — | 每行含 `state`/`phase`/`progress`/`delta_backlog`/`started_at`/`last_error` |

**失败可见性**：异步构建的失败（唯一冲突、schema 变化、提交失败）不会出现在 `CREATE INDEX` 的返回值里——
必须通过**轮询 `SHOW INDEXES`**（`state=ERROR` + `last_error`）获知。文档与 loader 必须按这个契约使用。

**为什么不复用 `QueryExecutor`**：它是"语句"执行器（持有语句级状态与计划缓存语义）；后台构建只需要数据/元数据门面 + 会话。把 builder 放在 `src/storage/index/` 可以避免把语句级语义带到后台线程，也避免旧方案里"从后台线程调用 executor"的隐式耦合。

---

## 13. 可观测、兼容性、测试

### 13.1 可观测

* `SHOW INDEXES` **追加**列（保持现有列顺序不变）：`phase`（SCAN/CATCHUP/FINALIZE/-）、`progress`（0–100）、`delta_backlog`、`started_at`、`last_error`。
  * `state` 列改输出**语义状态**：`BUILDING`（内部 `WRITE_ONLY` 不再泄漏）/ `PUBLIC` / `ERROR`。
  * Neo4j 兼容过程 `db.indexes` 保持词表：`ONLINE` / `POPULATING` / `FAILED`；**as-built**：
    `populationPercent` 仍按 state 给 0/100，尚未接运行时 progress（见 §13.2 未做项）。
* 指标（进程内计数器 + `/metrics` 或日志）：`index_build_running/queued`、`index_build_seconds`、`delta_replay_rows/rounds/conflicts`、`finalize_gate_wait_ms`、`finalize_drain_ms`、`finalize_timeout_total`、`orphan_tables`。
* 日志：每次状态迁移一行（图、索引、id、from → to、耗时、backlog、原因）。

### 13.2 兼容性

| 表面 | v2 行为 | 兼容性 |
|---|---|---|
| `CREATE INDEX` | **异步**：建定义 + 建表 + 入队后**立即返回** `Index created (building): <name>`；构建失败只在 `SHOW INDEXES` 可见（`state=ERROR` + `last_error`） | **破坏性**：依赖"返回即 PUBLIC"的客户端/脚本必须改为**轮询 `SHOW INDEXES`** |
| `DROP INDEX` | **异步**：删定义后立即返回 `Index dropped (dropping): <name>`，物理清理后台完成 | **破坏性**（返回文案与时机）；语义上"定义立即消失"可接受 |
| `SHOW INDEXES.state` | `BUILDING` / `PUBLIC` / `ERROR` | **破坏性**：检查 `WRITE_ONLY` 的脚本需改 |
| `db.indexes` | `ONLINE` / `POPULATING` / `FAILED`（不变），`populationPercent` 变成真实进度 | 兼容（Neo4j 词表不变） |
| loader | `createPrimaryKeyIndexes` 后**轮询 `SHOW INDEXES`** 到 `PUBLIC`（`ERROR` 即失败）再装载边；`after_load` 的 `CREATE INDEX` 后同样要等。**不加超时参数**：空表建索引接近瞬时，长期 `BUILDING` 属于缺陷 | **必改**（P1 内同步修改 `src/program/loader/csv_loader.cpp` 与 loader 文档） |
| TCK / benchmark / 测试工具 | 在依赖索引的计划前**自行轮询 `SHOW INDEXES`**；引擎不提供等待过程 | 外部约定（写入测试文档）；TODO：`CREATE INDEX … WAIT` |
| TCK | DDL 立即返回；依赖索引的计划会退化为全表扫（结果仍正确） | 结果兼容；断言"DDL 后索引立即可用"的用例需改为等待 |
| 重启后 `BUILDING` | 转 `ERROR`（保留定义） | 行为修复；文档说明 |
| KV 布局 | 新增 `idx_delta_<id>`；索引表布局不变 | 兼容 |
| 边索引 `index_id` | 从 0 变为真实 id（P0 修） | 修复；旧数据里 id=0 的边索引启动时置 ERROR |

**迁移清单（P1 必须一起改，否则"异步"会静默破坏 loader）**

1. `src/program/loader/csv_loader.cpp::createPrimaryKeyIndexes`：建索引后轮询 `SHOW INDEXES` 到 `PUBLIC`（`ERROR` ⇒ 带上 `last_error` 失败退出；**无超时参数**）；
2. `runStatements(after_load)`：对含 `CREATE INDEX` 的语句，在该阶段末尾统一轮询到 `PUBLIC`；
3. loader 文档 / `docs/program/usage/loader.md` / 集成测试：把"返回即 PUBLIC"的断言改为"await 后 PUBLIC"；
4. 仓库内测试工具（`tests/` 里依赖 `CREATE INDEX` 后立刻查询的用例）统一改为 await。

### 13.3 测试策略（判据先行；先让缺陷存在时失败）

> 详细用例（ID / 步骤 / fail-first 证据 / 目标二进制 / 执行矩阵）见
> [online-index-build-test-plan.md](online-index-build-test-plan.md)。下表是验收判据，测试计划把它展开成可执行用例。

| # | 用例 | 判据 |
|---|---|---|
| T1 | 构建期并发写入（INSERT/UPDATE 换键/DELETE/MERGE 全形态） | `PUBLIC` 后"索引查询 == 全表扫 + 过滤"**逐行一致**（不是比条数） |
| T2 | 准入线性化（测试钩子在 `tryEnter`/关闸/排空间插入暂停点） | 不存在"在最终排空之后才写 delta 的已提交事务"；`inflight` 在 commit/rollback 后**必然归零** |
| T3 | 唯一索引：构建期重复 / `PUBLIC` 后重复 | 构建期 ⇒ 构建 `ERROR` 且索引不可用；`PUBLIC` 后 ⇒ 写入被拒 |
| T4 | 空索引护栏 | 有数据无条目 ⇒ `ERROR`，绝不 `PUBLIC` |
| T5 | 崩溃：构建中 `kill -9` | 重启后 `ERROR`、`idx_delta_<id>`/部分索引表被清、规划器不用它 |
| T6 | 构建中 `DROP` / 构建中 `dropGraph` | 无崩溃、无 use-after-close、任务退出、表最终清理 |
| T7 | `DROP` 与长查询并发 | 查询结果正确（不读到半删）、DROP 最终成功或转 `PENDING_PURGE` |
| T8 | 多图同时构建同号索引 | 两图互不干扰（闸门/注册表隔离） |
| T9 | delta 重放与用户写冲突（同键高并发） | 无丢写/重复；记录用户重试次数；阈值内 |
| T10 | schema DDL 与构建并发 | 构建 `ERROR`，不置 `PUBLIC` |
| T11 | 会话池耗尽 | 构建退避、不崩、不丢写 |
| T12 | loader 顺序（先建索引→装载） | 装载期索引 `PUBLIC` 且被维护；`SHOW INDEXES` 可见 |
| T13 | 异步 DDL | `CREATE INDEX` 立即返回且 `SHOW INDEXES.state = BUILDING`；轮询可看到 `BUILDING → PUBLIC`；等待期查询结果与全表扫一致（不使用未 `PUBLIC` 索引） |
| T15 | 元数据版本绑定（§10.1） | 事务开始后索引转 `PUBLIC`：该事务内查询**不走该索引**且结果与全表扫逐行一致；下一事务能看到并走索引 |
| T16 | loader 等待流程 | PK 索引轮询到 `PUBLIC`（或 `ERROR` 即失败，无超时参数）后再写边：端点解析成功；不存在"未等待也能正常工作"的侥幸依赖 |
| T17 | 提交校验（按元素） | 事务写标签 L 期间 L 被删除（或属性被删）⇒ 提交被拒且为可重试错误；数据未落库 |
| T18 | 无关 DDL 不误杀 | 长写事务期间创建/删除**别的**索引或改动别的标签 ⇒ 该事务仍能提交；只读事务在任何 DDL 下都不被拒 |
| T19 | 破坏性 DDL 的 drain | 旧事务未结束前不物理删除元素；旧事务提交后才 purge；新事务立即看不到该元素 |
| T20 | 校验与发布的竞态 | 在“读版本/校验/提交”与“发布新版本”之间插入暂停点，穷举排列 ⇒ 不存在“校验通过后又按已删元素提交” |
| T14 | ASan/UBSan | 新增并发用例零报告；TSan 可选（闸门/注册表） |

**破坏性用例**（需要独立数据目录、可重复运行）：T5（kill -9）、T6（构建中关图）、T9（高冲突）。测试不得放进 tmpfs；同一服务端重复运行时库名/前缀隔离。

---

## 14. 实施阶段（每阶段独立可验证、可回退）

| 阶段 | 内容 | 验收 | 回退 |
|---|---|---|---|
| **P0 基座（无行为变化）** | `IndexRuntime`/注册表；`IndexWriteRouter` 统一 7 条写路径（此阶段仍"直写索引"）；修边索引 `index_id`；启动 `WRITE_ONLY→ERROR`；`indexIsLive` 收敛 | 现网测试全绿；新增"边索引 id != 0"回归；写路径单测覆盖 router 的四种 phase 分支 | 单独 revert；无持久化变化 |
| **P1a 在线构建（核心）** | delta 表 + 写入分流 + 会话级准入 + builder（SCAN/CATCHUP/FINALIZE）+ 最终化协议 + 空索引护栏 + 唯一两段校验。**此阶段 DDL 仍"语句等待终态"**，便于先验证构建正确性 | T1/T2/T3/T4 通过 | 开关关闭 ⇒ 回到 P0 的直写路径（保留旧实现一版） |
| **P1b 异步 DDL + schema 版本** | `CREATE/DROP INDEX` 改为投递即返回；`GraphSchema` 不可变快照 + `SchemaPin` + `version_refcount`/`min_alive_version`（§10.1）；touched 集合 + **按元素的提交校验**（§10.2）；**loader/测试改为轮询 `SHOW INDEXES`**（§13.2 迁移清单） | T13/T15/T16/T17/T18 通过；loader 在 sf0.1 上“建 PK 索引 → 轮询 PUBLIC → 装载”跑通 | 作为 P1 的独立提交，可单独回退到"语句等待" |
| **P2 生命周期与恢复** | 启动恢复（含表清理）、孤儿表登记/回收、`DROP` 两阶段 + 读准入、关图排空 | T5/T6/T7/T12 通过 | 开关关闭 ⇒ 恢复单阶段 DROP（记录风险） |
| **P3 可观测收尾** | `SHOW INDEXES` 追加列、`db.indexes` 真实 `populationPercent`、指标/日志 | 字段与指标文档同步；T13 的观测部分 | 纯增量 |
| **P4 灰度高可用收尾** | sf1 压测（构建期持续写、长事务、冲突率）、调参、TCK/ASan/UBSan 全闸门、文档定稿 | 性能与冲突率达标（见 §15）；全闸门绿 | — |

**实施偏差（记录）**：P0 的"router 统一 7 条写路径"与 P1a **合并**。原因：router 的 `BUILDING → delta`
去向依赖变更表；若在 P0 接线只能加一个临时的"legacy 直写"模式，属于一次性代码且会把"去向唯一"的判定弄脏。
因此 P0 实际交付 = 状态模型/注册表/DDL 生命周期接线 + 两处必修缺陷；router 与 delta 在 P1a 一起接线（行为在那一步才变）。

**P1 的判据必须"先失败"**：在实现前先用测试固定"当前行为会失败"的用例（如"构建期插入 5 条 ⇒ 索引 5 条"），再实现。

**为什么拆 P1a/P1b**：delta/准入/最终化是正确性核心，异步只是执行形态。先在"语句等待"下把 T1–T4 跑绿，
再一次性切换异步 + 元数据版本 + loader 迁移（三者互为前提：异步使索引上线时机不确定，元数据版本保证读者不吃到半成品）。
这样失败时能立刻区分"构建协议错"还是"异步语义错"。

---

## 15. 风险与度量

| 风险 | 度量 | 缓解 |
|---|---|---|
| 构建期写放大（索引插入 + delta 插入 ≈ 2×） | `delta_put_total` vs `index_put_total`；构建期写延迟 p50/p99 | 变更表页基本常驻；工作流"空表建索引"零开销；默认并发度保守（2） |
| FINALIZE 阻塞写入 | `finalize_gate_wait_ms` p50/p99/max | 前置条件把 backlog 压到很小；超时回退；闸门窗口目标 < 50ms |
| 重放与用户事务冲突 | `delta_replay_conflict_total`、用户语句重试率 | 短批次 + 整事务重试；出现用户可见重试 ⇒ 切 §19.2 备选 |
| builder 扫描争抢 IO/cache | 构建期查询 p99、WT cache eviction | 并发度可配；构建批次 sleep/yield 让路；`EUGRAPH_INDEX_BUILD_CONCURRENCY=1` |
| 长事务导致抖动 | `finalize_deferred_total`、`finalize_timeout_total` | 前置条件②；抖动可见 |
| 边索引历史 id=0 数据 | 启动告警计数 | P0 修分配 + 启动置 ERROR；用户 DROP 后重建 |

**验收口径**：性能结论用同二进制交错 A/B，注明轮数/warmup/机器状态；1–4ms 级差异不写成优化。

### 15.1 写准入、批量装载与已知风险（as-built）

#### 事务级写准入

`IndexWriteGuard` 现在绑定到**整个用户事务**（`GraphSession` 持有 `index_id → guard`），
不是单条写入：

- 同一事务已经持有某索引准入时，即使 FINALIZE 已关闸，后续写入仍继续写 delta；
  FINALIZE 的 `closeAndWait` 等的是整个事务，不会死锁；
- 没有事务准入的写者才 `co_await IndexRuntime::waitForAdmission()`（baton，可被
  PUBLIC/ERROR 唤醒）；不再使用 1s sleep 轮询；
- 事务已持有 A 索引准入、又遇到 B 索引关闸时立即返回 `RetryLater`，让事务回滚重试，
  **不等待**（否则 B 的 FINALIZE 等本事务、本事务等 B，死锁）；
- 批量入口在数据写入前预取本批标签相关索引的准入；拿到后 sync applier 即使遇到关闸
  也能继续写 delta。

#### 批量装载

`batchInsertVertices/Edges` 现在：

1. `begin_write` 后用原子 `schemaVersion()` 取 `{schema, generation, compat}`；
2. 通过 `ensureSchemaPin` 绑定 pin/tracker（构建会 drain 旧批量事务）；
3. 用**pin 住的 schema** 解析标签/索引定义；
4. 数据写入前预取相关索引准入；
5. 索引维护统一走 `applyIndexWriteSync`（delta/PUBLIC/skip），不再有绕过。

#### 仍需跟踪

| # | 风险 | 状态 |
|---|---|---|
| R1 | FINALIZE 写入等待上限 | **已修**：事务级准入 + baton，测试覆盖 >1s 等待 |
| R2 | 批量装载未 pin | **已修**：批量 pin/tracker + 预取准入 + sync applier；补并发用例 |
| R3 | 旧 pin drain 无上限 | **已加**：`EUGRAPH_INDEX_PIN_WAIT_TIMEOUT_MS`（0=无限）+ 超时 ERROR/日志；默认仍无限 |
| R4 | 唯一索引 PUBLIC 前不强制 | **保持语义**：文档说明；强约束用空表建唯一索引 |
| R5 | tracker 注入 | **已修**：DDL 无 tracker 直接失败；测试覆盖 |
| R6 | `__anon__` 全量 schema 拷贝 | 仍待 COW 子快照（见 §10.3） |
| R7 | `waitForAdmission` 不支持协作取消 | 构建失败/PUBLIC 会唤醒；客户端取消需等当前 FINALIZE 结束；后续接 CancellationToken |


---

## 16. 被否决的方案（记录原因，避免重走）

1. **关闸后写者直写索引**（旧 PR）：违反 I3，旧 delta 行会覆盖新写入。
2. **先翻 `PUBLIC` 再排空**（旧 PR）：违反 I4，失败时静默不完整。
3. **thread_local 事务**（旧 PR）：协程迁移 + 指针键永不擦除 + 地址复用 ⇒ 错误事务；main 已有 `GraphSession`，隔离单位应是会话。
4. **进程级、按 `index_id` 的闸门注册表**（旧 PR）：多图同号冲突。
5. **`CREATE INDEX` 异步但没有同步迁移 loader**（旧 PR）：loader 依赖 PK 索引创建后立即可用；异步本身没问题，问题是等待契约没跟着改。
6. **只给 `CREATE` 两条路径接闸门**：其余 5 条路径丢写。
7. **`ERROR` 保留部分索引表**：残留条目污染重建结果。
8. **shadow table + 原子换表**：对"新建索引"仍需要构建期写入的去处（否则构建期写入丢失），因此仍需 delta；只在"重建已有索引"时才有价值，留作后续可选优化。
9. **append-only 日志 + 水位**：体积随写入次数无界增长、需要额外排序/聚合、水位要持久化才能续建；与"本期不做续建"冲突。
10. **全局一张 delta 表**：DROP/重放/清理都要按索引过滤，耦合高；每索引一张更简单。

---

## 17. 附录

### 17.1 参数（默认值可调，均需进配置文档）

| 参数 | 默认 | 说明 |
|---|---|---|
| `index_build_concurrency` | 2 | 每图同时构建的索引数（1 = 串行） |
| `delta_threshold` | 1000 行 | 进入 FINALIZE 的 backlog 阈值（配合"连续 2 轮"） |
| `oldest_inflight_ms` | 200 | 前置条件②：最老在飞写事务年龄上限 |
| `index_write_wait_ms` | 1000 | 写者在 FINALIZE 期的等待上限；超时 ⇒ 语句可重试失败 |
| `drain_timeout_ms` | 5000 | 等待 `inflight == 0` 的上限；超时 ⇒ 回退 CATCHUP |
| `purge_wait_ms` | 5000 | `readers == 0` 的等待上限；超时 ⇒ `PENDING_PURGE` |
| `replay_batch_rows` | 1024 | 每批重放/扫描行数 |

### 17.2 状态迁移表

**以 §4.4（as-built）为准**；设计目标与当前实现的差异见 §4.4.4。
早期草案里的 `CATCHUP 阈值连续 2 轮`、`FINALIZE 超时回 CATCHUP`、`version_refcount`
等未实现，不要按那些描述做实现/评审依据。

### 17.3 术语

* **builder**：执行 SCAN/CATCHUP/FINALIZE 的后台协程。
* **admission / 准入**：写者取得"可写变更表且收尾方必须等我事务结束"的资格（I5）。
* **finalize line**：关闸成功的那一刻；线前写 delta，线后写索引（I3）。
* **delta**：变更表 `idx_delta_<id>`。

---

## 18. 已定决策与剩余待确认

**已定（本轮拍板）**

| # | 决策 |
|---|---|
| 1 | `SHOW INDEXES.state` 对外叫 **`BUILDING`**（内部 `WRITE_ONLY` 不泄漏）；`db.indexes` 保持 Neo4j 词表 `ONLINE`/`POPULATING`/`FAILED` |
| 2 | **`CREATE INDEX` 异步**：建定义/建表/入队后立即返回；构建在后台推进 |
| 3 | **`DROP INDEX` 异步**：删定义即返回，物理表在后台清理（§9.3） |
| 4 | **构建期写入不会因"与索引冲突"而失败**：不使用未 `PUBLIC` 的索引做唯一性检查、不拒绝写入；重复只在构建最终校验时暴露并让构建 `ERROR`（§8.4）。FINALIZE 期写者会等待 PUBLIC/ERROR（事务已准入则继续写 delta），不因索引构建失败而失败 |
| 5 | **对齐 F1 在线 schema 变更**（§10.1 / I11）：事务绑定一个 `SchemaPin`；系统允许 DDL 与长事务并存；**写入维护按注册表当前状态**，所以旧事务也不会漏维护新索引 |
| 6 | delta 重放冲突**不设数值门限**：先实现"应用即删"，P4 度量；只有出现**用户可见重试**才切 §19.2 备选 |
| 7 | **显式事务内执行 DDL 一律拒绝**（`DDL cannot run inside an explicit transaction`）；自动提交不受影响 |
| 8 | **不提供等待过程**：需要等索引就自行轮询 `SHOW INDEXES`。loader 在"建 PK 索引后 / `after_load` 后"自行轮询到 `PUBLIC`（`ERROR` 即失败），**不设超时参数**——空表建索引接近瞬时，长期 `BUILDING` 属于缺陷 |
| 9 | TCK / benchmark / 测试工具：执行前**自行轮询 `SHOW INDEXES`**（引擎不介入）；轮询示例与建议间隔写进 loader/测试文档 |
| 10 | `CREATE INDEX … WAIT` 语法糖：**列入 TODO，本期不做**（§19.3） |
| 11 | **schema 版本**：每图持久化 `schema_version`，只在**定义变更**时递增（§10.2 / I13）；事务开始绑定 `SchemaPin`，事务内固定 |
| 12 | **提交校验按 touched 元素（A 方案）**：只有写过东西的事务才校验；只读事务永不因版本被拒；索引增删不构成提交冲突 |
| 13 | **不采用 F1 的时间租约**：单进程可精确计数，用 `version_refcount`/`min_alive_version` 的确定性 drain 替代租约；破坏性 DDL 等旧版本归零，长事务不因时间被拒 |
| 14 | **`DROP INDEX` 的读保护本期做**：两阶段（DROPPING → 等引用该索引的旧版本归零 → PURGING）+ 读句柄；拒绝"直接删表" |
| 15 | **破坏性标签/属性 DDL 本期只做基础设施**：`schema_version`/`SchemaPin`/提交校验/版本计数落地；`DROP LABEL`/删属性本身留到 §19.4（当前无 Cypher/Thrift 入口） |
| 16 | **`SchemaChanged` 让显式事务整体失败**；自动提交语句返回可重试错误。`DDL cannot run inside an explicit transaction` 为语句级、不可重试 | 见 §10.4 拍板 1/2/6 |
| 17 | **事务持有不可变 schema 版本**：新标签/新属性/新索引对它在事务内不可见 | 见 §10.4 拍板 3 |
| 18 | **drain 默认无上限等待**（DDL 异步返回，后台等），只告警不强制删 | 见 §10.4 拍板 4 |
| 19 | **`__anon__` 属性软上限 + 告警**（默认 100k/图，可配），不拒绝新名字 | 见 §10.4 拍板 5 |
| 20 | **引擎不自动重试** schema 冲突；交给 SDK/loader | 见 §10.4 拍板 7 |
| 21 | **模块化约束**：运行时状态/路由/构建/服务分层，依赖方向 `common/types ← storage/index ← storage/data/query/graph`，禁止反向 | §12 模块划分 |

**无未决项**（本轮全部拍板；后续新问题另开条目）。

---

## 19. 备选与后续

### 19.1 重建已有索引：shadow table + 换表

对"已有 `PUBLIC` 索引需要重建"（例如编码升级），可构建到 `vidx_<new_id>`，完成后原子切换定义里的表名/ID，旧表延后删除。这样构建期写入继续走旧表（不丢），构建器只写影子表，**不需要 delta**。作为 P4 之后的优化，不在本期。

### 19.2 delta 重放与用户事务的冲突（是什么、怎么办）

**是什么**：追赶期间，builder 在一个短事务里"把 delta 行应用到索引 + 删掉该 delta 行"；如果此时某个**用户事务**
正在写同一个 `(索引键, 实体)` 的 delta 行，WT 的乐观并发会在提交时发现写写冲突，**后提交者回滚**。
回滚的可能是用户语句 ⇒ 该语句需要重试（`TxnErrorClass::is_retryable` 已有分类）。

**为什么可能发生**：同一热点键在构建期被频繁更新；冲突窗口 = builder 一个批次事务的持续时间（默认 ≤ 1024 行）。

**怎么办（本期）**

* builder 批次尽量短；冲突时 builder 整事务重试（`WT_ROLLBACK` 必须整事务重来，不能只重试当前 cursor 操作）；
* 用户语句沿既有重试路径处理；
* 记录 `delta_replay_conflict_total` 与用户语句重试率，P4 用真实负载度量。

**什么时候换方案**：只有**用户可见的重试/失败**成为问题（而不是计数器非零）时，才切换到下面的备选——
**追赶期不删行、值里带每索引递增 `seq`**：builder 只应用 `seq > 内存水位` 的行，收尾关闸后再一次性删除。
代价：追赶期每轮要多扫/过滤（或按 seq 建辅助索引），变更表更大；换来用户事务零冲突。

**明确不做的**：不在本期引入固定"冲突率阈值"配置——没有真实负载数据之前，任何数值都是猜测。
### 19.3 后续（TODO）：`CREATE INDEX … WAIT`

本期统一用 `SHOW INDEXES` 轮询，不加等待过程也不改语法。后续如果客户端普遍需要"一条语句建完再继续"，再加：

```text
CREATE INDEX idx FOR (n:Label) ON (n.prop) WAIT [timeout_ms]
```

语义 = 提交任务后阻塞到终态（`PUBLIC` ⇒ 正常返回；`ERROR`/超时 ⇒ 报错并带 `last_error`），
内部实现就是轮询 `SHOW INDEXES`。**语法兼容面**是推迟它的唯一原因（需要动 grammar + TCK 基线），不是实现难度。

### 19.4 后续：破坏性标签/属性 DDL

本期 `DROP LABEL` / 删属性没有 Cypher/Thrift 入口（`dropLabel` 只有存储层 API，[ddl.md](ddl.md) 仍是规划）。
**当前没有接线**：`SchemaGenerationTracker`/`waitNoPinsOlderThan` 只被 `DROP INDEX` 使用；
破坏性 schema DDL 的 drain 与提交校验尚未接到任何入口。开放该入口前必须完成，否则旧事务可能按旧版本
写已删元素，产生不一致数据。

等要开放时，按 §10.2 的两阶段实现：

1. 发布 delete-only 版本（新计划/新写入不再看到该元素，旧 `SchemaPin` 仍可用）；
2. 等 `min_alive_version >= drop_version`（后台 drain，不阻塞 DDL 语句返回）；
3. 排它锁内物理删除数据与定义；失败进孤儿/重试队列（幂等）；
4. 提交校验（§10.2）作为安全网：drain 超时被强制推进时，仍能拦住按旧版本写该元素的事务。

**属性删除的额外约束**：`Properties` 是按 prop id 定位的向量，删除属性**不得重编号**（只标记废弃/置空），
否则所有旧版本事务与已有数据都会错位。这条要在 [ddl.md](ddl.md) 里先定。

**与索引的依赖约束**（破坏性 DDL 必须一并处理）：

* 删除被索引引用的属性/标签：先要求 `DROP INDEX`（或把索引置 `ERROR` 并清理），否则索引会留下无法解释的旧条目；
* 重命名标签/属性：数据层可以只改元数据（prop id 不变），但**弱 accessor 索引按属性名解析**、语句按名字解析，
  必须单独设计（改名后重建受影响的索引），本期不做。

### 19.5 后续：崩溃续建（watermark）

若要支持"重启后从断点继续"，需要：

1. 每批扫描提交时把 `(index_id, last_scanned_entity_id)` 持久化到 meta（与批次提交同一事务，或紧随其后并允许重叠）；
2. 重启后校验：`schema_version` 未变、索引表/变更表仍在 ⇒ 从水位继续 `SCAN`；
3. 续建前做一次**重叠扫描**（从水位再往前一段，写入幂等）以容忍"数据提交了但水位没落盘"的崩溃；
4. 任何不一致（表缺失、版本变化、水位非法）仍然回到 `ERROR`。

本期不做：水位持久化让每批多一次 meta 写，且错误恢复路径最容易引入静默不一致；先保证"失败=重建"这一条绝对可靠。

## 20. 旧分支的处置

* `feature/online-index-build` / `docs/online-index-build-design`（PR #247）**废弃**，不再 rebase。
* 从旧分支**单独提取**（不与本设计绑定，各自独立 PR、独立回归）：
  1. 边索引 `index_id` 分配缺失（main 仍存在）——**P0 必修**。
  2. 边索引查询静默返回空（`ProjectionExtract` 收尾缺失，旧稿 §17/§17.2 有完整证据链）——需在 main 上复现后单独修。
  3. `IS NOT NULL` 剪枝覆盖显式标签（旧稿 §18/§18.1 有失败用例）——需在 main 上复现后单独修。
  4. 启动时 `WRITE_ONLY → ERROR`（本设计 P0 顺带做）。
* 旧分支的设计文档保留在 git 历史里，供追溯；其中**协议部分**以本文为准。

---

## 21. 参考

* **F1 在线 schema 变更**：*Online, Asynchronous Schema Change in F1*（VLDB 2013）——
  元素四态（absent/delete-only/write-only/public）、schema 变更串行、lease、数据重整异步、写防护（write fencing）。
  本文 §10.1/§10.2 借用其骨架，但用**确定性引用计数**替代时间租约（单进程前提，见 §10.2）。
* **PostgreSQL `CREATE INDEX CONCURRENTLY`**：两遍扫描 + 短暂锁；本文的"关闸 + 排空"对应其最后加锁阶段。
* **MySQL InnoDB online DDL**：log 表 + 最后加锁重放；本文的变更表对应其 online log。
* 仓库内相关文档：[index_design.md](index_design.md)（索引编码与扫描）、
  [session-cursor-pool-design.md](session-cursor-pool-design.md)（会话/事务/池）、
  [loader-design.md](../program/design/loader-design.md)（装载顺序与主键索引依赖）。
