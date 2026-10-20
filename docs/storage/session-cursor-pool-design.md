# 会话 / 游标 / 连接 / 事务的归属设计（定稿）

## 1. 目标与三条硬规则

* **目标**：每次语句/事务有**一个独占的会话**（WT session + 可变事务 + 它用到的 cursor）✓；
  会话**可复用**（池化 ✓）但**绝不同时被两处使用** ✓；提交**必须显式** ✓ 且失败**必须可见** ✓。
* **R1 创建者 ≠ 持有者**：池拥有会话 ✓，租约只"借用" ✓。
* **R2 单一持有 + 不并发使用**（debug 期断言 ✓）。
* **R3 显式收尾**：commit 是唯一成功路径 ✓；未提交就释放 ⇒ **回滚** ✓（绝不静默丢数据 ✗）。

## 2. 分层与所有权

```
GraphInstance（每图一份）
├─ WtConnectionStore data_conn / meta_conn   连接所有者 ✓（data / meta 各一）
├─ SessionPool       data_pool（/ meta_pool）拥有 GraphSession ✓
├─ SyncGraphDataStore / SyncGraphMetaStore   **纯读写**：op(GraphSession&, …) ✓
├─ AsyncGraphDataStore                       异步门面：构造即绑定 GraphSession ✓（for_session 轻量值）
└─ QueryExecutor                             会话驱动：acquire → begin → … → commit ✓
```

**声明顺序**（析构即逆序 ✓）：`session_pool` 必须声明在 `data_conn/meta_conn` **之后** ✓
⇒ 池先销毁 ⇒ **会话先于连接关闭** ✓（否则 `WtSession::~` 在死连接上崩溃 ✓ 真实事故 ✓）。

| 组件 | 只做 ✓ | 绝不做 ✗ |
|---|---|---|
| `WtConnectionStore` | open/close ✓、`make_session()` ✓、`checkpoint(WtSession&)` ✓ | 业务读写 ✗、持事务 ✗ |
| `SessionPool` | 创建/复用/销毁会话 ✓、有界 ✓ | 业务读写 ✗、开事务 ✗ |
| `GraphSession` | 开/提交/回滚事务 ✓、持有 `CursorProvider` ✓、`wt_session()`（friend 存储层）✓ | 直接读写业务表 ✗ |
| `CursorProvider` | 会话内 cursor 的获取/复用/关闭 ✓、借用归还 ✓ | 业务读写 ✗、开事务 ✗、跨会话共享 ✗ |
| `wt::table_*`（自由函数） | 表级 KV ✓、统一 `scanRange` ✓（session 由调用方传入 ✓） | 创建 session ✗、DDL 序列 ✗、业务语义 ✗ |
| `SchemaDdl` | 接收 `WtSession&` ✓、单连接内建/删/ensure 表的唯一入口 ✓、失败清理 ✓、schema 锁 ✓ | 业务读写 ✗、跨连接原子性 ✗ |
| `SyncGraphDataStore` | 给定 session 的 KV 读写 ✓ | 建 session ✗、开/提交事务 ✗、持事务表 ✗ |
| `AsyncGraphDataStore` | 异步 dispatch ✓、构造即绑定会话 ✓、`for_session()` 轻量值 ✓ | 管池 ✗、开事务 ✗ |
| `QueryExecutor` | 取租约 ✓、按语句决定 RO/RW ✓ | 自己建连接/会话 ✗ |

**管理类操作的会话**：不再有 `open_admin_session()` 概念；调用方显式用
`WtConnectionStore::make_session()` 创建 `WtSession` 并传入：
  - 数据 DDL：sync store 在方法内部创建局部 `WtSession`，传给 `SchemaDdl`；
    `SchemaDdl` 内部锁保证同连接 DDL 串行化 ✓；
  - 元数据 / Catalog / checkpoint：同样在操作内部/调用点创建局部 `WtSession`（RAII，随用随关 ✓）；
  - `SchemaDdl` 只接收 `WtSession&`，不创建会话、不知道连接 ✓。

## 3. 类型与接口（定稿）

```cpp
// ── 连接所有者 ───────────────────────────────────────────────────────────────
class WtConnectionStore {
public:
    bool open(const std::string& home, const std::string& extra_cfg = "");  // 幂等 ✓ WAL + fsync-on-commit ✓
    void close() noexcept;                                                  // 幂等 ✓
    WtSession make_session();          // 创建会话的**唯一入口** ✓
    bool checkpoint(WtSession& session);  // 显式传入会话 ✓
};

// ── 事务错误的**单一分类**（纯逻辑，可单测 ✓）────────────────────────────────
enum class TxnErrorClass { None, Busy, Rollback, CacheOverflow, Panic, Unknown };

// 只有 Busy 可重试（不毒化 ✓）；其余失败保守毒化 ✗
bool is_retryable(TxnErrorClass) noexcept;
bool poisons_session(TxnErrorClass) noexcept;

struct TxnOutcome {
    TxnState state;                    // None / Active / Committed / RolledBack / Failed
    int wt_error = 0;                  // WT 原始错误码
    std::string message;
    TxnErrorClass error_class = TxnErrorClass::None;  // 未分类的失败按 Unknown 处理 ✓
};

class StickyError {                    // 首错粘住；本事务出现过失败 ⇒ 禁止 commit ✓
public:
    bool has() const noexcept;         // 任何失败（阻止提交 ✓）
    bool fatal() const noexcept;       // 不可重试（会话毒化 ✓）
    TxnErrorClass error_class() const noexcept;
};

// ── cursor 所有权边界（P1-7 ✓）───────────────────────────────────────────────
// 句柄二选一：borrowed（归会话缓存，析构只归还）或 owned（扫描用，析构即 close）✓
class CursorHandle {                  // move-only；default/invalid 合法 ✓
public:
    WtCursor* get() const noexcept;
    bool valid() const noexcept;      // 打开失败 ⇒ false ✓
    int error() const noexcept;       // 打开失败的 WT 错误码（0 = 成功）✓
    void reset() noexcept;            // 幂等：归还或关闭 ✓
};

class CursorProvider {                // GraphSession 持有；默认 = 每会话缓存，未来可换 cursor 池
public:
    CursorHandle acquire(const std::string& table);       // 缓存（point op）✓
    CursorHandle acquire_scan(const std::string& table);  // 独立（扫描；可同表并发）✓
    void clear() noexcept;                                // 会话收尾/复用 ✓
    void release(WtCursor&) noexcept;                     // 借用句柄析构时归还 ✓
};

// ── 会话（1 个 WT session + 可变事务 + cursor provider）─────────────────────
class GraphSession {
public:
    GraphSession(WtSession session,
                 std::shared_ptr<CursorProvider> cursors = nullptr);  // null ⇒ 默认缓存 ✓

    bool begin_read();                 // 快照只读 ✓
    bool begin_write();                // 读写 ✓（不得并行子计划 ✗）
    TxnOutcome commit();               // 显式 ✓；**有粘性错误 ⇒ 拒绝提交 + 回滚** ✓
    void rollback() noexcept;          // 幂等 ✓；不洗白毒化 ✗
    void close() noexcept;             // 关全部 cursor → **后端事务仍开着则回滚** ✓
    void reset_for_reuse() noexcept;   // 清 StickyError + **复位 closed_** ✓（否则复用即失败 ✗）

    // 数据 op / 表原语失败 ⇒ 粘住；不可重试类同时毒化 ✓
    void fail(TxnErrorClass, int wt_error, std::string message);
    void fail_from_wt(int wt_error, std::string message);  // 统一经 classifyWtError ✓

    WT_SESSION* wt_session() const noexcept;              // 仅存储层 ✓
    CursorHandle acquire_cursor(const std::string& table);       // 会话级缓存 ✓
    CursorHandle acquire_scan_cursor(const std::string& table);  // 独立扫描 cursor ✓
    bool active() const noexcept;  TxnKind kind() const noexcept;  bool poisoned() const noexcept;
    bool transaction_failed() const noexcept;             // 本事务已失败 ⇒ 不允许提交 ✓
    const StickyError& error() const noexcept;
};

// ── 池（拥有会话）+ 非拥有租约 ───────────────────────────────────────────────
// 租约与池共享 `SessionPoolCore`（shared_ptr）✓ ⇒ 池先销毁、租约后释放也安全 ✓
class SessionPool {
public:
    SessionPool(std::size_t max_total, std::function<WtSession()> make_session);
    std::optional<SessionLease> acquire();   // 耗尽/已关闭 ⇒ nullopt ✓（不阻塞 ✗）
    void put(GraphSession& s) noexcept;   // s.close() ✓ → 毒化销毁 ✗ / reset_for_reuse + 归位 ✓
    void clear() noexcept;                // ★ 连接关闭前必须调用 ✓；**只关闭空闲会话** ✓
    std::size_t outstanding() const noexcept;  // 借出会话数；非零 ⇒ 不得关闭连接 ✓
    bool closed() const noexcept;
};

class SessionLease {                       // move-only ✓ **非拥有** ✓；持 shared_ptr<Core> ✓
public:
    GraphSession& operator*() const noexcept;
    GraphSession* operator->() const noexcept;
    void release() noexcept;               // 幂等 ✓；析构自动归还 ✓
};

// ── 表级原语：无状态自由函数（session 由调用方传入 ✓）────────────────────
namespace wt {
WtCursor open_cursor(WT_SESSION*, const std::string& table);
bool table_put(WT_SESSION*, …);  std::optional<std::string> table_get(WT_SESSION*, …);
bool table_put(GraphSession&, …);  std::optional<std::string> table_get(GraphSession&, …);
// 区间/前缀迭代的唯一原语 ✓（tableScan 与 index range 共用）
int scan_range(WT_CURSOR*, start, exclusive, end, end_view, callback);
}

// ── DDL 序列（**唯一入口** ✓；锁在内部）────────────────────────────────────
class SchemaDdl {
    std::recursive_mutex mu_;                 // 取代 SchemaGate ✓
public:
    bool create_tables(WtSession&, std::span<const std::string_view>);  // 失败只回滚本次新建 ✓
    bool drop_tables(WtSession&, std::span<const std::string_view>, DropMode);
    bool ensure_tables(WtSession&, std::span<const std::string_view>);  // EBUSY 视为成功 ✓
};

// ── store：持有连接指针 + SchemaDdl（无 WtTableOps 成员）───────────────────
class SyncGraphDataStore {
    WtConnectionStore* conn_ = nullptr;
    SchemaDdl schema_ddl_;
public:
    bool ensure_tables();                        // 内部 make_session() → schema_ddl_.ensure_tables(session, …) ✓
    bool createLabel(LabelId);                   // 内部 make_session() → schema_ddl_.create_tables(session, {…}) ✓
    bool dropLabel(LabelId);                     // 内部 make_session() → schema_ddl_.drop_tables(session, {…}) ✓
    bool createIndex(const std::string&);        // 内部 make_session() → schema_ddl_.ensure_tables(session, {table}) ✓
    // 数据 op：op(GraphSession&, …) ⇒ wt::table_put/… ✓
};

// ── 存储层 / 门面 / 执行器 ───────────────────────────────────────────────────
// Sync*：op(GraphSession& session, …)  ⇒ session.wt_session() / session.acquire_cursor()
//        扫描用 acquire_scan_cursor()；区间迭代统一走 wt::scan_range() ✓
// Async：op(GraphSession& session, …)  ⇒ io_.dispatch([&]{ return shared_.op(session, …); })
//         for_session(GraphSession&) → 会话绑定的**轻量值**（三个引用，无堆/无空态）✓
// Executor：auto lease = pool.acquire(); lease->begin_write(); …; lease->commit();
```

**跨 store 边界**：data / meta 是两个 WT 连接，**没有**跨连接原子性 ✗。保持“元数据先写、
数据表后建”，由 CREATE 路径的幂等 ensure 兜底 ✓（见 §5 备注）；`SchemaDdl` 只保证单连接内
一组表的“尽力原子” ✓。

## 4. 生命周期与收尾顺序

1. **租约获取**：`SessionPool::acquire()` → 池内空闲表取一个（或新建 ✓）→ `SessionLease` 借出 ✓。
2. **事务开始**：`GraphSession::begin_read/begin_write` ✓（打开 WT 事务 ✓）。
   - 已 Active 时再次 begin **失败且不得清 StickyError**（否则后续 commit 会提交部分写入 ✗）；
   - 初始 `begin` 只有在 `TxnController::begin` 成功后才清上一个事务的非致命错误 ✓。
3. **执行**：op 用 `session.wt_session()` 与 `session.acquire_cursor(table)` / `acquire_scan_cursor(table)` ✓
   （cursor 随会话关闭 ✓；打开失败经 `CursorHandle::error()` 显式可见 ✓）。
4. **提交**：`session.commit()` ✓ —— 本事务出现过任何失败（`StickyError::has()`）⇒ **拒绝提交并回滚** ✓；
   提交失败 ⇒ 按 `TxnErrorClass` 处理（`Busy` 不毒化 ✓，其余毒化 ✗）。
5. **归还**：`~SessionLease` → `SessionPoolCore::put(session)` ✓：
   `session.close()`（关 cursor + **后端事务仍开着 ⇒ 回滚** ✓）→ 毒化 ⇒ **销毁不回池** ✗ →
   池已关闭 ⇒ 销毁 ✓ → 否则 `reset_for_reuse()` + 归位 ✓。
   租约持 `shared_ptr<SessionPoolCore>` ⇒ **池对象先销毁也不会悬空** ✓。
6. **图关闭**：先 `usage->waitDrained()` 等在飞使用者归零 ✓ → `session_pool.clear()` ✓
   （只 close **空闲**会话；借出会话由租约释放时 close ✓）→ 再关连接 ✓。
   若等不齐 ⇒ **故意泄漏实例** ✓（绝不关仍有使用者的连接 ✗）。

## 5. 事务语义与错误处理

| 情况 | 处理 ✓ |
|---|---|
| 正常提交 | `commit_transaction` 返回 0 ⇒ `Committed` ✓ |
| 数据 op / 表原语失败 | 一律 `session.fail_from_wt()` 粘住 ✓；**即使调用方忽略返回值，commit 也会被拒绝并回滚** ✓ |
| `WT_ROLLBACK` / `WT_CACHE_OVERFLOW` | **整个事务失效** ✗ ⇒ 会话毒化 ✓；由**语句边界**决定重试 ✓（≤3 次 + 退避 ✓） |
| `WT_PANIC` | 引擎不可信 ✗ ⇒ 会话**销毁** ✓ + 停止服务 ✓ |
| `WT_BUSY/EBUSY` | 可重试 ✓（**不毒化** ✓）；本事务仍禁止提交（回滚后会话可复用 ✓） |
| 未知错误码 | 保守毒化 ✓ |
| 未提交就释放 | 自动回滚 ✓ + 告警 ✓（绝不静默丢数据 ✗） |

**失败可见性硬规则**：事务内任何一次失败都不得被后续成功的 `commit()` 掩盖 ✗。
`TxnController` 用 `transaction_open` 区分「逻辑状态」与「后端事务是否仍开着」：
毒化后仍需 `rollback()` 收尾 ✓（否则泄漏 WT 事务）。`rollback_transaction` 本身失败 ⇒
会话毒化并粘错，池销毁它（绝不带着未收尾事务回池 ✗）。

**读写混合必须同一事务** ✓（MVCC：跨事务写入不可见 ✓ 因为 `commit_ts > read_ts` ✓）。

## 6. 并发规则

* WT session/cursor **不是线程安全的** ✗，但**无线程亲和** ✓ ⇒ 允许**顺序**换线程使用 ✓，**禁止并发** ✗。
* 会话**不跨线程并发** ✓：一个租约同一时刻只属于一个执行流 ✓。
* **无 TLS** ✓（session 由池分发 ✓）。
* **锁表**：池一次 mutex（每次 acquire/put ✓，**不是**每 KV 调用 ✗）；cursor 缓存在会话内 ✓（无锁 ✓）。

## 7. 性能约定

* 热路径不加锁 ✓（每语句一次 acquire ✓ + 每次 op 一次 dispatch ✓）。
* **无 cursor 池** ✓（实测：池化 cursor −8~15% 且 2 次崩溃 ✗）；cursor 按**会话**缓存 ✓，随事务/会话关闭 ✓。
* **NUMA 暂未接入**：旧 `acquire()` 的 8 个桶实际全部传 0，已删除该参数；
  等真正按调用线程的 current-node 分配时再加 ✓。
* 故障**快速失败** ✓（`StickyError` 首错粘住 ✓）。

## 8. 多副本（Phase C，延后）

决策已记录、暂不实现 ✓：逻辑 redo log（1 WT txn = 1 条 ✓）、大事务分块 + 原子翻转 ✓、
WT 时间戳（`commit_ts = f(term,index)` ✓、`stable_ts` = 多数派已提交 ✓）、
快照 = checkpoint + `applied_index` 原子目录切换 ✓、索引在日志里只记 DDL、条目各副本本地重建 ✓（配 `index_consistent_up_to` + 规划期读闸 ✓）。
参考 MongoDB 的时间戳纪律/bookmark/大事务分块/初始同步 ✓；**不照搬**异步主备回滚 ✗ 与 capped oplog ✗。

## 9. 测试与验证要求

* **测试数据目录不得放 tmpfs** ✗（`/tmp` 默认即 tmpfs）：WT 在空间不足时会 `WT_PANIC`，表现为难以归因的假失败 ✗。
* **会话生命周期测试必须先单跑**、并在需要时用 ASan 取栈 ✓；**提交前必须看到 `PASSED` 汇总** ✓。
* 关键不变量须有**能区分对错**的用例 ✓：会话复用后仍能 `begin/commit` ✓、未提交释放后会话可复用 ✓、
  资源在连接关闭前释放 ✓、**池先销毁/租约后释放不悬空** ✓、**clear() 不关闭借出会话** ✓、
  **数据 op 失败后 commit 被拒绝** ✓、**Busy 失败不毒化** ✓、
  **cursor 打开失败可见且阻止提交** ✓、**borrowed 句柄只归还一次** ✓。

## 10. 术语表（固定用语）

| 术语 | 含义 |
|---|---|
| `WtConnectionStore` | 连接所有者（每图 data/meta 各一）；只 open/close/`make_session()`/`checkpoint(WtSession&)` ✓ |
| `WtSession` | WT session 的 RAII 句柄（1 个 `WT_SESSION*`）✓ |
| `GraphSession` | 一次租约的独占事务作用域（1 个 `WtSession` + 可变事务 + `CursorProvider`）✓ |
| `SessionPool` / `SessionLease` | 会话池（拥有）/ 非拥有租约（借用，析构归还）✓ |
| `CursorProvider` / `CursorHandle` | 会话内 cursor 的所有权边界：provider 拥有，handle 借用或独占 ✓ |
| `AsyncGraphDataStore` | 异步门面：**构造即绑定**一个语句的 `GraphSession`；`for_session()` 返回轻量值 ✓ |
| `SessionScope`（Bolt） | 显式事务作用域：图租约 + 会话租约（BEGIN…COMMIT）✓ |
| `SchemaDdl` | 单连接 DDL 唯一入口（接收 `WtSession&`；内部 schema 锁 + 建/删/ensure）✓ |
| `wt::table_*` | 表级 KV / `scan_range` 的无状态自由函数（session 显式传入）✓ |

## 11. 备注：后续可选重构（**未实施**，供评审）

> 以下只是对当前实现的评审建议，**不是** §1–§9 的定稿内容，也**没有落地**。
> 采用其中任何一项前，先更新 §3 的接口定义，再按 §9 的验证要求分批实施。
> 排序：P0 = 安全/正确性，P1 = 职责/正交性，P2 = 接口卫生。

### P1-8 元数据 / Catalog 的多键原子性

**现状**：[sync_graph_meta_store.cpp](../../src/storage/meta/sync_graph_meta_store.cpp) 每个
`metadataPut/Get/Scan` 各创建一个局部 `WtSession`；`AsyncGraphMetaStore::persistLabelDef()` 连续写
`M|label:` 与 `M|label_id:` 两个 key、两个 autocommit 事务；Catalog 的 next-id 也是“读—写”。
**现在** `persistLabelDef`/`saveNextIds` 会检查 `metadataPut` 返回值并在失败时抛错、回滚内存
schema（不再静默成功 ✓）；但两个 key 仍是两个 autocommit 事务，**跨 key 原子性**仍未解决。

**建议**：给 meta 一个显式 `WtSession&` 参数或 `with_session(fn)`（可在单个显式事务里做多键写入）；
或按 §2 预留的 `meta_pool` 建元数据会话池。Catalog 的 next-id 分配至少放进同一显式事务
（目前靠 `GraphManager::createGraph` 的写锁串行化）。

### 建议顺序

1. **P1-8** 复用显式 session 的 `with_session(fn)`，把多键写入收进单事务；
2. 语句边界重试语义（Busy 的“首行前重试”还是交给协议层）需要先定，再实现。

**语句边界重试**（`Busy` ⇒ ≤3 次 + 退避）尚未实现：当前 `Busy` 只保证不毒化，语句仍以明确错误结束。
