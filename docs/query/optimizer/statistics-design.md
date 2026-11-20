# 统计信息（P1：节点计数）设计草案

- 状态：**设计草案，未实现**（评审通过后再动代码）
- 日期：2026-11-20
- 关联提交：`b7ce18d`（顶点统计接入 Catalog）、`e0fd394`（启动期扫描边统计）
- 适用版本：当前 `perf/pc-bound-dst-expand` 分支

## 1. 背景与目标

### 1.1 现状（临时实现）

`GraphManager::openGraphInstanceUnchecked` 在图打开时**同步**扫描：

- 每个标签的 `label_fwd_{id}` 表 → 顶点数（SF0.1：15 标签 / 623,747 顶点 / **60 ms**）
- 每个边标签的 `etype_{id}` 表 → 边数 + 平均出度（SF0.1：1,477,965 边 / **481 ms**）

问题：启动路径上做 O(N) 扫描；多图线性叠加；写入后统计过期（仅启发式）。

### 1.2 P1 目标

把**节点计数**（每个标签的活顶点数）改为持续维护，使读取为 O(1)：

- CBO 得到与数据一致的基数，而不是每次打开扫一遍；
- 为后续 `MATCH (n:Label) RETURN count(n)` 走计数计划（P1.5）打基础。

### 1.3 非目标

| 项 | 归属 |
|---|---|
| 把启动扫描移到后台线程（止血） | **P0**，独立小改动 |
| 关系计数（按 type、按 (srcLabel,type,dstLabel)） | P2，需要新的锁粒度 |
| `count()` 查询改写为计数计划 | P1.5（依赖 P1 的表结构） |
| CBO 的 join 实现 / 方向候选 | 另一条线 |

## 2. 关键前提（已核实）

1. **语句级事务**：`QueryExecutor` 对每条语句 `begin_transaction(isolation=snapshot)`（`query_executor.cpp:196`），提交在 `StreamContext::commitAsync()`（`query_executor.hpp:138`）；显式 `BEGIN/COMMIT` 时语句不自行提交。
2. **删除是硬删**：`SyncGraphDataStore` 直接从 `label_fwd_{id}` / `vertex_data` 删行（`sync_graph_data_store.cpp:1098`），没有墓碑，因此"计数 = 表行数"语义干净。
3. **两条 WT 连接**：数据（`data_conn`）与元数据（`meta_conn`）分离，跨连接**没有事务原子性**。
4. **已有启动期全扫能力**：`scanVerticesByLabel`，可复用作重建与校验。
5. **当前不支持 `SET n:Label`**（binder 无 AddLabel），所以 P1 的计数变化只来自建点/删点。若将来支持标签变更，同一套 delta 接口可扩展。

## 3. 设计决策

### D1 计数表放在**数据连接**，不放 meta

跨连接无原子性，一旦计数在 meta、数据在 data，就必然出现"数据提交了、计数没写"的窗口，需要额外补偿逻辑（Neo4j 的 `OutOfOrderSequence` + reverse recovery 就是在解决这类问题）。放在数据连接，至少"计数落盘"与"数据落盘"共享同一个 checkpoint 边界。

### D2 运行期以**内存为准**，checkpoint 落盘（不做写直达）

这是本设计的核心取舍。

写直达（每条语句在事务里 `读-改-写` 计数行）看似最自然，但会让**同一标签的所有写入串行化**：所有建 Person 的事务都要改同一行 `node_counts[label=Person]`，WT 会在这行上产生写冲突，并发写吞吐直接塌陷。

因此采用：

| 阶段 | 行为 | 代价 |
|---|---|---|
| 语句执行 | 只累加**内存 delta**（不写表） | O(1)，无热 key |
| checkpoint（已有 60s 周期） | 把绝对计数写入 `table:node_counts`（每标签一行） | O(#labels) 次写 |
| 干净关闭 | 最后一次 checkpoint 保证落盘 | — |
| 打开 | 读表（O(#labels) 次点查，< 1 ms） | 极小 |
| 崩溃后打开 | 检测到非干净关闭 ⇒ **扫一次重建** | 60 ms（SF0.1）/ 秒级（SF1），仅崩溃时 |

代价是"崩溃后要扫一次"。这是有意的：崩溃是低频事件，而热 key 冲突是持续成本。

### D3 计数语义

- 统计"活顶点"：`CREATE` +1、`DELETE` -1；
- 未知标签返回 0，但通过 `stats_ready` 标志区分"表里没有"与"统计未就绪"——未就绪时 CBO 退回默认值，而不是认为该标签为空。

## 4. 数据结构

### 4.1 表

```cpp
// src/common/types/constants.hpp
constexpr const char* TABLE_NODE_COUNTS = "table:node_counts";
```

- **键**：`u32 label_id`（big-endian，与现有 KeyCodec 风格一致）
- **值**：`{u8 version = 1} {u64 count}`（小端或 BE 与现有编码保持一致）
- 单表而非每标签一表：标签数量级 1e2，重建/校验一次前缀扫描即可；每标签一表会让表数量随 schema 增长。

### 4.2 内存缓存

```cpp
// GraphInstance
std::shared_mutex counts_mu;
std::unordered_map<LabelId, uint64_t> node_counts;  // 权威（运行期）
bool counts_ready = false;                          // false ⇒ CBO 用默认值
```

### 4.3 增量收集

```cpp
struct CountsDelta {
    std::unordered_map<LabelId, int64_t> node_delta;
};
```

挂在 **`GraphSession` 的事务状态**上，而不是 `QueryContext`：显式事务里多条语句必须累积到同一个 delta，生命周期跟随 `BEGIN…COMMIT`。

## 5. 写入路径

| 位置 | 改动 |
|---|---|
| `CreateNodePhysicalOp` | 每成功创建一批，按 label 累加 `+N` |
| `DeletePhysicalOp` | 删除顶点前按它的 label 累加 `-N` |
| `GraphSession::commit()` | 提交前把 delta 应用到**内存缓存**（用 `counts_mu` 排他锁），不写表 |
| `GraphSession::rollback()` | 丢弃 delta，缓存不动 |
| checkpoint | 把缓存里的绝对计数写入 `TABLE_NODE_COUNTS` |

**顺序要求**：缓存更新必须在 WT 提交**之后**——否则提交失败时缓存已经脏了。因此提交路径改为：

```cpp
auto out = session_->commit_transaction(...);
if (out.committed()) counts_cache.apply(delta);   // 仅在成功后
```

> 注：这条顺序有一个已知窗口——提交成功、进程在更新缓存前崩溃，磁盘上数据已变而缓存没有。重启后由"崩溃重建"覆盖（D2），因此可接受。

## 6. 读取路径

- `GraphManager` 打开图：读 `TABLE_NODE_COUNTS` → 填 `GraphInstance::node_counts`，`counts_ready = true`；表缺失/版本不符/非干净关闭 ⇒ 走重建。
- `QueryExecutor` 建 Catalog：用 `node_counts` 填 `catalog->setLabelStats(...)`，替换现在从 `GraphSchema::label_vertex_counts` 取值的路径。
- `GraphSchema::label_vertex_counts` 保留为**兼容字段**（边统计仍走它），P1 完成后可标记 deprecated。

## 7. 重建与校验

| 场景 | 行为 |
|---|---|
| 表不存在 / 版本不符 / 标签集合不匹配 | 自动重建（一次全扫，事务内写绝对值） |
| 非干净关闭（WT recovery） | 自动重建 |
| 运维主动 | 建议命令：`CALL db.stats.rebuild()`（Bolt 扩展），或启动参数 `--rebuild-stats` |
| 一致性检查 | `CALL db.stats.verify()`：对比扫表结果与表值，**只报告不自动修** |

重建耗时基准（SF0.1）：节点 60 ms；重建必须持有图级写闸门，避免与在线写并发。

## 8. 并发与可见性分析

| 场景 | 结果 |
|---|---|
| 两个语句并发建不同标签的点 | 各自 delta 累加，映射不同 key，无冲突 |
| 两个语句并发建**同标签**的点 | 都改内存同一个 entry ⇒ `counts_mu` 排他，最后值 = 两者之和 ✓（增量可交换） |
| 提交失败 / 回滚 | delta 丢弃，缓存不动 |
| 缓存更新后进程崩溃 | 重启重建（D2） |
| 读者与写者并发 | 读侧 `shared_lock`，拿到的是一致快照（计数单调变化，不存在"半个事务"） |

**风险点**：`counts_mu` 在任何写语句提交路径上都会被持有一次。临界区只有几次哈希查找，但需在压测里确认不会成为写入瓶颈（预估 < 100 ns，可接受）。

## 9. 失败矩阵

| 故障 | 影响 | 处理 |
|---|---|---|
| WT 提交失败 | delta 未应用 | 语句失败；缓存/表均未变 ✓ |
| checkpoint 写计数失败 | 磁盘计数落后 | 记录告警；下次 checkpoint 重试；崩溃后重建兜底 |
| 缓存更新与提交之间有崩溃 | 磁盘数据更新、缓存未更新 | 重启重建 ✓ |
| 重建中途失败 | 表部分写入 | 重建在单事务内完成，失败整体回滚；保留 `counts_ready=false` |
| 版本升级 | 解码失败 | 识别版本字节 ⇒ 触发重建 |

## 10. 验收标准

**功能**
1. 建 N 个点后 `node_counts` 与 `MATCH (n:Label) RETURN count(n)` 一致；
2. 删 M 个点后一致；
3. 事务回滚后计数不变；
4. 两个并发事务各建 100 个同标签点，最终计数 = 200；
5. 崩溃（kill -9）后重启，重建后计数正确。

**性能**
6. 打开时间：**节点计数部分**由 60 ms 降到 < 5 ms（读表）；崩溃场景重建 60 ms。
   注意：边统计的 481 ms 不在 P1 范围内，仍占启动时间，需靠 P0（移到后台）解决；
7. 写路径：建点吞吐下降 < 2%（对照现有 LDBC 装载 70 s 基线）；
8. `checkpoint` 增加时间 < 1 ms（15 个标签）。

**可观测**
9. 建点/删点/重建/校验均有日志与计数；
10. 暴露 `stats_ready`、`last_rebuild_at`、`last_verify_result` 三个指标（PROFILE 或系统表）。

## 11. 风险与非目标

| 风险 | 说明 | 缓解 |
|---|---|---|
| 崩溃后重建 | 大图（SF1+）秒级 | 可配置为后台重建，期间 CBO 用默认值 |
| 计数漂移 | 任何漏计都会长期积累 | 提供 `verify()`；CI 加"装载后校验"用例 |
| 显式事务跨语句 | delta 生命周期必须跟事务，不是语句 | 评审确认放在 `GraphSession` 事务状态 |
| 边计数仍靠扫描 | P1 不涉及 | `EUGRAPH_STATS_SKIP_EDGES` 暂留；P2 处理 |

**明确不做**：关系计数、三标签组合计数、`count()` 计划改写、统计的自动周期校验。

## 12. 待评审的开放问题

1. **delta 挂载点**：`GraphSession` 事务状态 vs `QueryContext`（显式事务跨语句的情形必须支持前者）。倾向：`GraphSession`。
2. **崩溃检测方式**：用 WT 的 recovery 标志，还是自己在 meta 写"clean shutdown"标记？倾向后者（简单、可读）。
3. **checkpoint 落盘的写放大**：每标签一行、60 s 一次，可接受；若标签数上千需改批量。
4. **是否 P1 顺带做 `count()` 计划**：倾向不做，避免一条分支同时改存储与规划器。
