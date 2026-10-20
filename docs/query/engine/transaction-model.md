# 事务模型

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航
>
> 所有权与生命周期的**唯一事实来源**是
> [storage/session-cursor-pool-design.md](../../storage/session-cursor-pool-design.md)（§1–§9）。
> 本文只描述查询引擎看到的事务流程，不重复接口定义。

---

## 一、事务基础

### 分层

- **连接**归 `WtConnectionStore`（每图 data / meta 各一）✓：只负责 open/close、checkpoint、`make_session()`、
  管理类由持有连接的 store 在方法内部 `make_session()`，再传给 `SchemaDdl`/`wt::table_*` ✓。
- **事务**归 `GraphSession`（1 个 WT session + 可变事务 + 会话级 cursor 缓存）✓：
  `begin_read()` / `begin_write()` / `commit()` / `rollback()` / `close()`。
- **池**归 `SessionPool`（每图一个）✓：拥有并复用全部 `GraphSession`；`acquire()` 返回**非拥有**的
  `SessionLease`，析构即归还。
- **存储层**是纯读写 ✓：`ISyncGraphDataStore` 的每个数据操作都吃 `GraphSession&`，内部只用
  `session.wt_session()` 与 `session.acquire_cursor(table)` / `acquire_scan_cursor(table)`；
  不建会话、不开/提交事务、不持有事务表。
- **异步门面** `AsyncGraphDataStore` 只做 dispatch ✓：`for_session(GraphSession&)` 返回绑定该会话的 facade，
  所有 op 在该会话上执行。

### 隔离与错误

- 隔离级别：`isolation=snapshot`（WiredTiger snapshot isolation）。
- 提交必须显式 ✓；未提交就释放 ⇒ 池在 `GraphSession::close()` 中**回滚** ✓（绝不静默丢数据）。
- **失败即禁止提交**：数据 op / 表原语失败经 `session.fail_from_wt()` 粘住；即使调用方忽略了
  `bool/optional` 返回值，`commit()` 也会拒绝提交并回滚 ✓（绝不把部分写入当成功 ✗）。
- 提交失败按 `TxnErrorClass` 处理：`Busy` 可重试、**不毒化**；`Rollback` / `CacheOverflow` /
  `Panic` / 未知 ⇒ 毒化（`TxnOutcome` / `StickyError` 首错粘住），毒化会话**销毁不回池**；
  语句边界决定是否重试（重试 ≤3 次 + 退避尚未实现：`Busy` 目前只保证不毒化，语句仍以明确错误结束）。
- 毒化 ≠ 事务已结束：`TxnController::transaction_open()` 仍为真时，`close()` / `rollback()` 必须
  回滚后端事务 ✓（否则泄漏 WT 事务）。

---

## 二、语句事务流程

`prepareStream()` 自己取租约并按语句类型决定 RO / RW：

```
pool.acquire() → session.begin_read()/begin_write()
                   → async_data.for_session(session) → 物理计划/执行
                   → StreamContext::commit() / rollback()
                   → ~SessionLease → pool.put(session)
```

- `StreamContext` 持有 `std::optional<SessionLease>`；提交/回滚由协议层在流结束时调用
  `StreamContext::commit()` / `rollback()`。
- **顺序红线**：先销毁算子树（`gen` / `phys_op`，关闭其中游标）再提交/回滚。会话一旦归还，
  池可能关闭 cursor；逆序会造成 `~WtCursor` 访问已关闭会话。
- 计划阶段出错 ⇒ `ctx->rollback()`（或租约析构时的兜底回滚）。
- EXPLAIN / DDL 等不产生数据流的结果在返回前显式回滚。

### 显式事务（Bolt BEGIN…COMMIT）

显式事务跨多条语句共享**同一个**会话：

- 第一条语句前由 `GraphService::beginSession()` 取租约并 `begin_write()`，同时持有图使用租约；
- 后续语句通过 `SessionRef`（`std::optional<std::reference_wrapper<GraphSession>>`）传入
  `prepareStream()`，语句**不** begin/commit；
- `COMMIT` ⇒ `session.commit()`；`ROLLBACK` / 断连 / RESET ⇒ `rollback()` 并归还会话；
- 语句失败 ⇒ 整个显式事务回滚（Neo4j 语义）。

---

## 三、流式模式事务

```
prepareStream() → StreamContext{session lease, query_store, gen} → 协议层消费
```

- generator 正常耗尽 ⇒ `StreamContext::commit()`；
- 客户端断连 / generator 提前销毁 ⇒ 销毁算子树后 `StreamContext::rollback()`；
- 任何“丢掉流”的路径都走 `BoltSession::abandonStream()` / `StreamAbandonRollback`，不允许裸丢会话。

---

## 四、批量操作事务

`GraphService::batchInsertVertices` / `batchInsertEdges` 各取**一个**会话、一个事务：

```
pool.acquire → begin_write → async_data.for_session(session)
             → 读预检（主键 first-wins）+ 写实体 + 写索引条目 → commit
```

读预检与写在同一事务（MVCC：跨事务写入不可见）。提交失败 ⇒ 明确抛错，
绝不把“提交失败”当成成功返回。

---

## 五、DDL 事务特性

DDL 操作（createLabel、createIndex 等）**非事务性**：

- create/drop table 是管理类操作，由 store 在方法内部创建局部 `WtSession`（RAII，
  与用户事务严格分离）；
- label/edge label 的元数据写入是元数据 store 的管理类 KV 操作；
- **CREATE 的幂等 ensure**：标签可能只有元数据而数据表尚未建。节点路径由 `meta.createLabel()`
  返回既有 id、再 `store.createLabel(id)` 保证；边路径由 `CreateEdgePhysicalOp` 在首行处理前
  显式 `store.createEdgeLabel(id)` 补齐（`createTable` 对已存在表返回 EBUSY 并视为成功）✓。
  缺失表上的写入会粘住会话并阻止 commit，绝不静默提交部分索引写入 ✗；
- CREATE INDEX 同步回填使用独立会话 + 独立事务；
- DROP INDEX 不实际删除 WT 索引表（仅删除元数据）。

---

## 六、Schema 与事务一致性

每个语句开始时加载 schema 快照（label_defs、edge_label_defs），语句全程使用同一份快照：

- **只读语句**：物理计划在事务开始时一次性构建，WT snapshot isolation 保证数据一致性；
- **读写语句**：读路径（物理计划）和写路径（索引维护）使用同一份 schema。

DDL 状态变更（如索引状态从 WRITE_ONLY → PUBLIC）不影响已在运行的事务——它们持有旧 schema 的副本。

---

## 七、易错点

1. **会话必须先于连接销毁**：`GraphInstance` 中 `session_pool` 声明在 `data_conn/meta_conn` 之后；
   显式关闭时先 `session_pool->clear()` 再关连接。否则 `WtSession::~` 在死连接上崩溃（真实事故）。
2. **一个租约同一时刻只属于一个执行流**：WT session/cursor 不是线程安全的；允许顺序换线程，
   禁止并发使用。
3. **先拆流，再结束事务**：见 §二 的顺序红线。
4. **毒化会话不回池**：提交失败的会话必须销毁，否则后续语句在该会话上快速失败。
5. **批量操作使用独立事务**：不与外层语句共享事务（与设计一致）。
