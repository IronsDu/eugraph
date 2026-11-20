# 在线索引构建 v2 测试计划（判据先行）

> 配套设计：[online-index-build-design.md](online-index-build-design.md)（§13.3 的 T1–T20 是验收判据，本文把它们展开成**可执行用例**）。
> 状态：**待评审**。评审通过后作为 P0–P4 的测试契约。
>
> 三条原则：
> 1. **判据先行**：每个"修缺陷"的用例先证明缺陷存在时会失败（fail-first）；新能力用例先明确"在旧实现上必然不成立"的观测。
> 2. **判据独立**：索引结果不能拿索引自己验证——oracle 是**全表扫**（去掉索引后重跑）或 **EXPLAIN 计划**；不是"条数相等"。
> 3. **确定性**：不拿 `sleep` 当时钟。并发/窗口用**测试钩子**控制顺序，断言的是不变量与逐行结果。

---

## 1. 测试分层与目标

| 层 | 目标（CMake target，见根 `CMakeLists.txt`） | 覆盖什么 | 运行成本 |
|---|---|---|---|
| 单元 | `index_runtime_tests`（新）、`index_write_router_tests`（新）、`index_delta_tests`（新） | 状态机、准入守卫、router 去向、delta 编解码/重放语义 | 毫秒级 |
| 存储集成 | `index_store_tests`（扩）、`index_codec_tests`（不动） | delta 原语、按键序续扫、应用后删除、去重 | 秒级 |
| 服务集成 | `index_build_service_tests`（新）、`index_build_task_tests`（新） | 队列/并发度/取消/相位机/发布回调、同步 runner | 秒级 |
| 执行器 | `index_e2e_tests`（扩 55+）、`query_executor_tests`（扩） | DDL 语义、构建期写入、唯一索引、EXPLAIN、事务绑定 | 十秒级 |
| 图级 | `restart_persistence_tests`（扩）、`test_catalog_and_graph_manager`（扩） | 启动恢复、多图隔离、关图/关实例 | 十秒级 |
| 崩溃/端到端 | 新脚本 `tests/index_build_chaos_test.py`（或 shell）+ `eugraph-server` 子进程 | kill -9、构建中 DROP/关图、孤儿回收 | 分钟级（L3） |
| 语义门禁 | TCK（受影响 feature 子集 → 全量）、`scripts/verify_ldbc_results.py` | DDL 语义与查询结果不回归 | 分钟–小时 |

**新增 target 命名**：`index_runtime_tests`、`index_write_router_tests`、`index_delta_tests`、`index_build_service_tests`；沿用现有 `index_e2e_tests` / `index_store_tests` / `restart_persistence_tests` 扩用例。

---

## 2. 必须先建的测试基础设施（P0 的一部分）

### 2.1 确定性钩子 `IndexTestHooks`

没有钩子就只能靠 sleep，测试必然 flaky。设计一个**生产代码里恒存在、默认 no-op** 的钩子（只多一次空 `std::function` 检查）：

```cpp
// src/storage/index/index_test_hooks.hpp（示意）
namespace eugraph::testhooks {
// 命名暂停点：测试可 install 一个 handler，在 handler 里阻塞直到测试放行
void point(std::string_view name, uint64_t index_id);
void install(std::function<void(std::string_view, uint64_t)>);
void clear();
}
```

必须支持的点（按用例需要）：

| 点 | 位置 | 用途 |
|---|---|---|
| `scan.after_read_props` | builder 读完实体属性、写索引条目前 | 制造"回填读到旧值"的窗口（P1a-04） |
| `scan.batch_committed` | 每批提交后 | 控制扫描推进节奏 |
| `catchup.batch_applied` | 重放批提交后 | 观测轮次/backlog |
| `finalize.gate_closed` | 关闸成功、等 inflight 前 | 校验"关闸后写者等待"（P1a-11/12） |
| `finalize.before_public` | 校验通过、落 PUBLIC 前 | kill/崩溃窗口（P1a-13） |
| `admission.enter` | `tryEnter` 前后 | 准入线性化排列（P1a-11） |
| `commit.before_validate` | 提交校验与 DDL 发布之间 | 竞态用例（P1b-06/T20） |

### 2.2 oracle：索引结果 vs 全表扫

统一 helper（放 `tests/test_index_oracle.hpp`）：

```cpp
// 1) 用 EXPLAIN 断言"这条查询确实走了索引"（否则用例无效）
// 2) 记下索引路径结果
// 3) DROP INDEX 并轮询 SHOW INDEXES 直到定义消失（已是异步语义）
// 4) 同一条查询重跑（规划器回退全表扫），逐行逐列比对
EXPECT_TRUE(planUsesIndex(executor, query, index_name));
auto with_index = execSync(executor, query);
auto without    = execSyncAfterDrop(executor, query, index_name);
EXPECT_EQ(with_index.rows, without.rows);   // 逐行，不是 count
```

> 为什么不用"另建一张无索引图"：数据复制成本高且容易失去可比性；"删索引后重跑"是同一份数据、同一条 SQL，最直接。
> 为什么必须先断言 EXPLAIN：否则查询可能本来就没走索引，用例就变成了自欺欺人。

### 2.3 数据目录

现有 `test_index_e2e.cpp` 用 `std::filesystem::temp_directory_path()`（经常是 tmpfs）。**并发/崩溃/大数据用例必须落到磁盘目录**：

* **已实现**：`tests/test_index_env.hpp` 的 `makeTempDir` 支持 `EUGRAPH_TEST_DATA_DIR`；
  默认仍用 `$TMPDIR`，并发/崩溃/大数据用例应显式设置到磁盘目录；
* 每个用例目录名带 pid + 用例名，启动前清理、结束删除；避免污染共享库。

### 2.4 崩溃子进程 harness

* **已实现** `tests/index_build_chaos_test.py`：起 `eugraph-server`（临时图目录）→ 加载 N 行 →
  异步 `CREATE INDEX` → 确认 `BUILDING` 后 `SIGKILL` → 用同一 data-dir 重启 → shell 查 `SHOW INDEXES`
  断言 `ERROR`（绝不 `PUBLIC`）；手动 L3：
  `python3 tests/index_build_chaos_test.py --server build/release/eugraph-server --shell build/release/eugraph-shell --rows 100000`。
* 同样 harness 覆盖：构建中 `DROP INDEX`、构建中 `SIGTERM`、`DROP DATABASE`。
* 放 L3（分钟级），不进每次增量。

### 2.5 时间无关

* 所有等待用钩子/条件变量，不用 `sleep_for` 断言状态；
* 唯一允许的时间断言是**性能上界**（P4），且必须同机、同二进制、注明轮数；
* 超时类配置（`drain_timeout_ms` 等）在单测里设成 0/很小，只断言**回退路径**，不断言时长。

---

## 3. P0 用例（基座：行为不变 + 两个必修缺陷）

| ID | 用例 | 目标 | 判据 | 缺陷先验 / 说明 |
|---|---|---|---|---|
| P0-01 | `IndexRuntime` 状态迁移 | `index_runtime_tests` | 合法迁移通过、非法迁移拒绝；`plannerUsable` 只在 `PUBLIC∧ACTIVE`；`PURGING` 终态 | 新代码单测 |
| P0-02 | 准入守卫幂等/计数 | `index_runtime_tests` | 同一 session 对同一 index 重复 `tryEnter` 只计一次；commit/rollback/close 后 `inflight==0`；无守卫泄漏 | 新代码单测；对应旧方案 P0-1 死代码教训 |
| P0-03 | router 去向全分支 | `index_write_router_tests` | SCAN/CATCHUP→delta；FINALIZE→`RetryLater`（绝不直写索引）；PUBLIC→索引；ERROR→skip；unique 只在 PUBLIC 查重；多索引按 id 升序 | fake store/session；分支穷举 |
| P0-04 | **边索引 `index_id` 分配** | `index_e2e_tests` | `CREATE EDGE INDEX` 后定义 `index_id != 0`；`SHOW INDEXES` 可见；重启后仍 `!= 0` | **fail-first（main 上必然失败：当前恒 0）** |
| P0-05 | 启动恢复：构建中索引 | `restart_persistence_tests` | 手工把索引置 `WRITE_ONLY` → 重开图 → 变 `ERROR`；部分索引表/变更表被清；`EXPLAIN` 不使用 | main 上会永远停在 `WRITE_ONLY`（fail-first） |
| P0-06 | `index_id==0` 防御 | `restart_persistence_tests` | 造一条 id=0 的边索引定义 → 启动置 `ERROR` + 告警；不路由到 `idx_delta_0` | 防御性用例 |
| P0-07 | 现有回归 | 全部 index/query 套件 | 全绿（行为保持） | P0 是重构，不允许行为漂移 |

**P0 的完成判据**：P0-04/P0-05 在实现前先绿？不——先提交为失败用例（或 `DISABLED_` + 本地已证失败），实现后转绿；其余全绿。

---

## 4. P1a 用例（构建协议；此阶段 DDL 仍"语句等待终态"）

| ID | 用例 | 目标 | 判据 | 缺陷先验 / 说明 |
|---|---|---|---|---|
| P1a-01 | 空表构建 | `index_e2e_tests` | 0 行 → 快速 `PUBLIC`；delta 表已删；索引可用 | 基线 |
| P1a-02 | 基础回填正确性 | `index_e2e_tests` | 预置 N 行（含缺属性、复合、weak/strong accessor）→ 索引路径 == 全表扫**逐行** | 现有能力回归 |
| P1a-03 | 唯一索引历史重复 | `index_e2e_tests` | 预置重复 → 构建 `ERROR`，错误含冲突键/实体；索引不可用；**绝不 PUBLIC** | main 已如此（回归） |
| P1a-04 | **构建期 UPDATE 产生过期条目** | `index_e2e_tests` + 钩子 | 钩子在 `scan.after_read_props` 暂停 → 事务把键 A→B → 放行 → `PUBLIC` 后：`WHERE key=A` 不得返回该实体；索引 == 全表扫逐行 | **fail-first**：main 直写索引会留下 A 条目（错结果）；本用例必须先在 main 上跑出失败 |
| P1a-05 | 构建期换键链 A→B→C | `index_e2e_tests` | 最终只有 C→实体；A/B 无残留；delta 行 A:DEL、B:PUT→B:DEL、C:PUT 语义正确 | 逐行比对 |
| P1a-06 | 构建期 DELETE / 墓碑 | `index_e2e_tests` | 删除后索引无该实体；重复删除/重放幂等 | 逐行比对 |
| P1a-07 | 构建期边写入（含 payload） | `index_e2e_tests` | 边索引结果 == 全表扫逐行；邻接 payload 正确 | 边路径单独覆盖（旧方案漏过） |
| P1a-08 | 构建期唯一重复 | `index_e2e_tests` | 构建窗口内两个事务插同键（都成功）→ 构建 `ERROR`；`PUBLIC` 后同键插入被拒；删键后可再插 | 与 §8.4 一致 |
| P1a-09 | 空索引护栏 | `index_e2e_tests` + 故障注入 | 扫描有值实体 >0 但产出 0 条 → `ERROR`，绝不 `PUBLIC` | 回归历史事故 |
| P1a-10 | 提交失败不 PUBLIC | `index_store_tests`/故障注入 | 注入 commit 失败 → `ERROR` + 错误信息；无 `PUBLIC` | 回归 |
| P1a-11 | **准入线性化** | `index_runtime_tests` + 钩子 | 在 `admission.enter`/`finalize.gate_closed` 排列：不存在"关闸后写 delta 的已提交事务"；in-flight 归零 | 核心不变量（I5/I6） |
| P1a-12 | 收尾超时回退 | `index_build_service_tests` | 长事务持准入 → `drain_timeout` 到 → 回 `CATCHUP` + 指标；事务结束后再收尾成功；业务写不被永久阻塞 | 与 §8.3 一致 |
| P1a-13 | 崩溃：构建中 kill -9 | chaos harness | **SCAN / CATCHUP / FINALIZE 三个阶段各 kill 一次**：重启后 `ERROR`、delta/部分表已清、规划器不用、可重建；`last_error` 说明重启中断 | 对应 §9.1；FINALIZE 用例覆盖"数据已提交但状态未翻"仍置 `ERROR` |
| P1a-14 | 并发构建受控 | `index_build_service_tests` | N 个 submit：running ≤ 2、FIFO、全部 `PUBLIC`；**FINALIZE 串行**（同一时刻 ≤1 个 CLOSING） | §8/§11 |
| P1a-15 | delta 重放语义 | `index_store_tests` | 键序扫描、分批续扫、应用后删除、覆盖去重、DEL 幂等 | 与旧方案一致 |
| P1a-16 | WT 冲突重试 | `index_store_tests`（压力） | 同键高并发：builder 整事务重试；最终无丢写/无重复；记录冲突计数 | P4 再收紧 |
| P1a-17 | **兼容新增不中止构建** | `index_e2e_tests` | 构建中：自动建别的标签、给别的标签追加属性、建/删别的索引 ⇒ 构建继续、最终 `PUBLIC`、结果逐行正确 | 隐式建标签/`__anon__` 加属性是运行时行为，若用"全局版本变了就失败"会误杀 |
| P1a-17b | **依赖元素变化中止构建** | `index_e2e_tests`（或合成单测） | 被索引的属性被删/改类型、索引定义变化 ⇒ 构建 `ERROR`，不 `PUBLIC` | 破坏性 DDL 未开放时用合成 schema 单测覆盖判定 |
| P1a-18 | 多图同号索引 | `test_catalog_and_graph_manager` | 两个图各自 `index_id=1` 同时构建 → 互不干扰（闸门/注册表按图隔离） | 旧方案进程级闸门的教训 |
| P1a-19 | 构建不阻塞写入 | `index_e2e_tests` | 钩子暂停 builder 在扫描中 → 期间写入/查询全部成功（不靠 sleep 断言） | §3 目标 1 |

**P1a 完成判据**：P1a-04（fail-first）转绿 + P1a-02/03/05/06/07/08/09 全绿。

---

## 5. P1b 用例（异步 DDL + schema 版本）

| ID | 用例 | 目标 | 判据 | 说明 |
|---|---|---|---|---|
| P1b-01 | `CREATE INDEX` 立即返回 | `index_e2e_tests` | 语句返回 `Index created (building)` 且 `SHOW INDEXES` 立刻 `BUILDING`；轮询到 `PUBLIC`；等待期查询 == 全表扫（不使用未 PUBLIC 索引） | 新语义 |
| P1b-02 | `DROP INDEX` 异步 | `index_e2e_tests` | 立即返回；`SHOW INDEXES` 定义消失；同名可立即重建；旧表后台清理（orphan/pending 可观测） | 新语义 |
| P1b-03 | `SHOW INDEXES` / `db.indexes` 文案 | `query_executor_tests` | `SHOW INDEXES.state`: `BUILDING`/`PUBLIC`/`ERROR`；`db.indexes`: `POPULATING`/`ONLINE`/`FAILED` + `populationPercent` 0→100 | 兼容面 |
| P1b-04 | loader 流程 | `index_e2e_tests` + loader 集成用例 | 建 PK 唯一索引 → 轮询 `PUBLIC` → 装载顶点/边；端点解析成功；重复键被拒 | §13.2 迁移清单 |
| P1b-05 | **事务内不升级版本** | `query_executor_tests` | `BEGIN` → 期间另一连接建索引并等 `PUBLIC` → 本事务 `EXPLAIN` **不含**该索引；`COMMIT` 后下一个事务 `EXPLAIN` 含；两次结果逐行一致 | §10.1 / T15 |
| P1b-06 | **提交校验（按元素）** | `index_runtime_tests`（合成 schema）+ `query_executor_tests` | 合成两版 schema：写到的 label/property 被删 ⇒ `compatible=false`；未涉及的变更 ⇒ true；索引变更 ⇒ true | T17/T18；破坏性 DDL 未开放，先用单测覆盖判定函数 |
| P1b-07 | 无关 DDL 不误杀 | `query_executor_tests` | 长写事务期间建/删**别的**索引或改别的标签 ⇒ 提交成功 | T18 |
| P1b-08 | 只读事务永不因版本被拒 | `query_executor_tests` | 任意 DDL 下只读事务正常提交 | T18 |
| P1b-09 | 事务内 DDL 拒绝 | `query_executor_tests` | `BEGIN` 内 `CREATE/DROP INDEX` ⇒ 明确错误；自动提交正常 | §10.1 |
| P1b-10 | 计数器持久化/单调 | `restart_persistence_tests` | 重启后 `schema_generation` 与 `compat_version` 都不回退；无 key 的老库从 0 开始；**索引状态翻转不递增 `compat_version`、递增 `schema_generation`** | I13 |
| P1b-11 | 唯一性按注册表 | `index_e2e_tests` | 旧版本事务在索引已 `PUBLIC` 后写重复键 ⇒ 被拒；索引无重复 | §10.2 末 |
| P1b-12 | 提交/发布竞态 | `index_runtime_tests` + 钩子 | 在 `commit.before_validate` 与 DDL 发布间排列 ⇒ 不存在"校验通过后按已删元素提交" | T20 |
| P1b-13 | **隐式新增：自动建标签 / `__anon__` 属性** | `query_executor_tests` | `CREATE (n:NewLabel {foo:1})`：`compat_version` 不变、`schema_generation` +1；无事务失败；**同一事务后续语句能看到 NewLabel / foo**（语句边界采用兼容新增）；旧事务的查询按自己的视图报 `Label not found`（语句错误，非提交失败） | 对应用户问的"隐式添加列/建标签" |
| P1b-14 | **兼容新增不递增兼容版本** | `query_executor_tests` | 追加属性、建/删索引、索引转 PUBLIC、`__anon__` 加属性后断言 `compat_version` 不变、`schema_generation` 增长；长事务仍走快路径（可用计数器/提交路径观测） | §10.3 分类表 |
| P1b-15 | **索引可见性冻结** | `query_executor_tests` | 事务开始时索引为 `BUILDING`；期间转 `PUBLIC`；本事务 `EXPLAIN` 不含它；新事务含；两者结果逐行一致 | I11 / T15 |
| P1b-16 | **pin 生命周期与会话复用** | `query_executor_tests` / `session_pool_tests` | BEGIN 取 pin、commit/rollback/close 释放；会话归还池后新事务**不复用**旧 pin/准入；无泄漏（计数归零） | A1/A2 |
| P1b-17 | **隐式 schema 不随事务回滚** | `query_executor_tests` | `BEGIN; CREATE (n:NewLabel {foo:1}); ROLLBACK;` ⇒ 标签与 `__anon__` 属性仍在（新事务可见），但没有数据行 | A5 |
| P1b-18 | **并发自动建同名标签** | `query_executor_tests`（并发） | N 个并发语句建同一新标签 ⇒ 同一个 label id、无 duplicate 错误、表只建一次、数据不丢 | B1 |
| P1b-19 | **`__anon__` 属性并发分配** | `index_store_tests` / meta 测试 | 并发不同名字 ⇒ 唯一 prop id；同名 ⇒ 同一 id；持久化失败回滚内存定义（可注入） | B2 |
| P1b-20 | **`SchemaChanged` 作用域** | `query_executor_tests` | 按 A3 决定：显式事务整体失败（客户端可重试整个事务）/ 自动提交语句可重试；不出现“沿用旧 pin 重试必失败” | A3 |
| P1b-21 | 错误映射 | `tests/bolt` 驱动用例 | `SchemaChanged` / `DDL cannot run...` 映射为可重试/明确错误码（按 D3） | D3 |

---

## 6. P2 用例（生命周期与恢复）

| ID | 用例 | 目标 | 判据 |
|---|---|---|---|
| P2-01 | DROP 与长查询 | `index_e2e_tests` + 钩子 | 查询扫描中 `DROP` → 查询结果正确、无崩溃；DROP 等版本引用归零后清理 |
| P2-01b | **DROPPING 期新计划不用它** | `index_e2e_tests` | `DROP` 返回后：新事务的 `EXPLAIN` 不含该索引；**旧事务里发起的新语句**的 `EXPLAIN` 也不含它（即使其 pin 里还是 PUBLIC）；两者结果都 == 全表扫 |
| P2-02 | 构建中 DROP | chaos harness | 取消 + builder 退出 + 表清理；定义消失；无 use-after-close |
| P2-03 | FINALIZE 中 DROP | `index_build_service_tests` | 闸门复位、不卡死、状态非 `PUBLIC` |
| P2-04 | 孤儿表 | `restart_persistence_tests` | 占住表 → DROP 留 orphan 记录；重启回收；记录清空 |
| P2-05 | pending_drop 崩溃恢复 | chaos harness | 删定义后 kill → 重启清表、清记录（幂等） |
| P2-06 | 关图/关进程 | chaos harness | 构建中 `dropGraph`/`SIGTERM` → 无 WT panic、无 use-after-close |
| P2-07 | `DROP DATABASE` 与构建 | `test_catalog_and_graph_manager` | 图使用闸门等构建退出后再关连接 |
| P2-08 | drain 策略 | chaos/集成 | 长事务持有 pin 时破坏性 DDL 不物理删除；超时行为符合配置（按 E3 决定，默认“等待+告警、不强制”） |

---

## 7. P3/P4 用例（可观测、性能与闸门）

**P3**

| ID | 用例 | 判据 |
|---|---|---|
| P3-01 | `SHOW INDEXES` 字段 | `phase`/`progress`/`delta_backlog`/`started_at`/`last_error` 在各阶段取值合理；`ERROR` 带原因 |
| P3-02 | `db.indexes` 进度 | `populationPercent` 单调 0→100；`PUBLIC` 时 100 |
| P3-03 | 日志/指标 | 每次状态迁移一行日志；关键计数器递增（若已接入 metrics） |

**P4**

| ID | 用例 | 判据 |
|---|---|---|
| P4-01 | sf1 构建期持续写 | 构建期持续 INSERT/UPDATE/DELETE；`PUBLIC` 后索引 == 全表扫逐行；记录重放冲突率与最终化 p99 |
| P4-02 | 长事务 + drain | 长事务不因时间被拒；破坏性 DDL 的后台 drain 不阻塞 DDL 语句返回 |
| P4-03 | 会话池耗尽 | 构建退避、无崩溃、无丢写 |
| P4-04 | ASan/UBSan（+ 可选 TSan） | 新增并发用例零报告 |
| P4-05 | TCK 全量 | 31/31 场景、144/144 步骤（或当前基线） |
| P4-06 | neo4j 对照 | `scripts/verify_ldbc_results.py` 对索引路径与全表扫一致 |

---

## 8. 重点用例的详细步骤（评审重点）

### 8.1 P1a-04：构建期 UPDATE 的过期条目（fail-first 核心）

```text
1. 建标签 Person(name)，插入 1000 个顶点（email=e000..e999），建非唯一索引 idx_email。
2. 钩子在 scan.after_read_props 暂停 builder（此时 builder 已读到 person#7 的 email=e007）。
3. 用户事务 UPDATE person#7 SET email='changed'（走 BUILDING ⇒ 写 delta：e007:DEL + changed:PUT）。
4. 放行 builder。等待 PUBLIC。
5. 断言：
   a) EXPLAIN "MATCH (p:Person) WHERE p.email='e007' RETURN p" 确认走 idx_email；
   b) 该查询返回 0 行（main 直写索引会返回 1 行 ⇒ 失败）；
   c) 索引路径与"DROP 后全表扫"逐行一致。
```

### 8.2 P1a-11：准入线性化

```text
用钩子在 W1.tryEnter / B.close / B.drain / W2.tryEnter 之间插入事件；
枚举 {W1 在关闸前准入, W2 在关闸后到达} 的组合，断言：
  - 每个已提交写者要么"准入成功且被排空"，要么"等待后在 PUBLIC 下直写索引"；
  - 不存在"关闸后才写 delta 且已提交"的事务；
  - closeAndWait 返回 true 时 inflight==0 恒成立（断言协议，不断言时长）。
```

### 8.3 P1b-05：事务内不升级元数据

```text
连接 A：BEGIN；执行一条读（建立数据快照）。
连接 B：CREATE INDEX idx_x ...；轮询 SHOW INDEXES 直到 PUBLIC。
连接 A：EXPLAIN 依赖 idx_x 的查询 ⇒ 计划中不得出现 idx_x；执行查询 ⇒ 结果 == 全表扫。
连接 A：COMMIT。
连接 A（新事务）：EXPLAIN 同一查询 ⇒ 必须出现 idx_x；结果与之前逐行一致。
```

### 8.4 P1b-06：提交校验（合成 schema 单测，不依赖破坏性 DDL）

```text
构造 v1/v2 两份 GraphSchema：
  v2 = v1 + 新标签；v2b = v1 - 标签 L；v2c = v1 上改索引定义
断言 compatible(v2, touched={L}) = true（追加属性/新增标签不算冲突）
     compatible(v2b, touched={L}) = false
     compatible(v2c, touched={index_i}) = true
再补一个执行器级用例：事务写 L 期间用测试 API 把 L 标记删除（若暂不可用，留 TODO 到 §19.4）
```

### 8.5 P1a-13：崩溃恢复（chaos harness）

```text
1. 起 server（磁盘目录），建大标签数据，CREATE INDEX。
2. 等日志出现 "phase=SCAN/CATCHUP"（或钩子点），kill -9。
3. 重启 server；SHOW INDEXES ⇒ 该索引 state=ERROR；索引表/delta 表不存在。
4. 依赖该索引的查询：EXPLAIN 不使用；结果与全表扫一致。
5. 重新 CREATE INDEX ⇒ 成功、PUBLIC、结果正确。
```

### 8.6 P2-04：孤儿表回收

```text
1. 用长事务占住索引表（扫描中）。
2. DROP INDEX（异步）⇒ 定义消失、表删除失败 ⇒ orphan 记录出现。
3. 结束长事务，重启 server ⇒ 启动日志出现"孤儿表已回收"；orphan 记录为空。
```

---

## 9. 执行矩阵（按 AGENTS.md 验证阶梯）

| 级别 | 何时跑 | 内容 | 目标耗时 |
|---|---|---|---|
| **L0** | 每次改动 | 单元三层（runtime/router/delta）+ 编译目标 | < 2 min |
| **L1** | 每个完整增量 | `index_store_tests`、`index_build_service_tests`、`index_e2e_tests` 的受影响子集 | 2–10 min |
| **L2** | 功能完成/开 PR | 全量 `index_*`、`query_executor_tests`、`restart_persistence_tests`、受影响的 TCK feature 子集 | 10–20 min |
| **L3** | PR ready / merge 前 | chaos harness（kill -9/关图/DROP DATABASE）、全量 TCK、ASan/UBSan（优先 CI） | 30–60 min |

**CI 分工**：ASan/UBSan/TCK 交给现有 CI（与 main 一致的闸门）；chaos harness 建议新加一个 CI job（或 nightly），因为它要起子进程且耗时。

---

## 10. 明确不覆盖（本期）

* **多进程/多实例共享同一图**：当前单进程模型；`schema_version` 的跨实例租约不在本期（§10.2 / §19.4）。
* **构建续建（水位）**：本期崩溃一律 `ERROR` + 重建。
* **零阻塞 DROP**：只保证正确性 + 有界等待。
* **单个索引内并行扫描**：并发度只作用于"索引之间"。
* **性能数值断言**：P4 只记录/对比，不设硬阈值（避免机器漂移导致 flaky）。

---

## 11. 评审时需要确认的取舍

1. ~~是否接受 `EUGRAPH_TEST_DATA_DIR`~~：已实现；默认仍 `$TMPDIR`，L3 用例显式设置到磁盘。
2. 是否接受 **生产代码里的 no-op 测试钩子**（`IndexTestHooks`）；替代方案是编译期开关，但会让测试与 CI 用两份构建，不推荐。
3. chaos harness 用 **Python 子进程**（与现有 `tests/bolt` 一致）还是 shell；建议 Python（可复用驱动/断言库）。
4. P1a-04 的 fail-first 是否作为 **P1a 的开工第一件事**（必须先看到 main 上失败，再实现）。

## 实现与验证记录（2026-11）

| 阶段 | 证据 |
|---|---|
| P0 | 注册表/路由/applier 单测（22/10/10）、边索引 index_id 持久化、重启 BUILDING→ERROR（restart 5/5） |
| P1a | delta 原语 + 写入分流 + FINALIZE 关闸/排空；并发 UPDATE/INSERT/晚到写入、唯一并发重复 → ERROR（index_e2e 74/74） |
| P1b | schema 代数 + SchemaPin + 规划期可见性；异步 CREATE INDEX（构建服务）；loader 轮询；`SHOW INDEXES` BUILDING |
| P2 | 异步 DROP + pending_drop/orphan + 启动回收；读句柄 + 旧 pin drain；PENDING_PURGE 5s 周期清扫；构建中 dropGraph 用例 |
| P3 | `SHOW INDEXES` 追加 phase/progress/delta_backlog/started_at/last_error；构建期 backlog=2、追平归零 |
| P4 | 事务级 schema 版本 pin（旧版本在 DDL 后仍存活）通过；chaos harness（100k 行构建中 SIGKILL → 重启 ERROR）通过；schema 快照独立拷贝 + 并发属性追加读不崩（`SchemaSnapshotIsIndependentCopy`、`SchemaSnapshotsSurviveConcurrentPropertyAdds`）；builder 可脱离 Executor 直接运行（`BuilderRunsWithoutQueryExecutor`）；并发压力（默认 4000 行、50k 行参数化）通过；ASan（address）`index_runtime_tests` 22/22、`index_e2e_tests` 74/74 + stress 通过；TCK `show-indexes` 1/1、`string-index-boundaries` 6/6 |

运行命令：

```bash
# 压力
EUGRAPH_INDEX_STRESS=1 ./build/release/index_e2e_tests --gtest_filter='*StressConcurrentBuild*'
# ASan（本机 GCC 16 的 ubsan 运行库缺失，先用 address-only）
ASAN_OPTIONS=detect_leaks=0 ./build/asan/index_e2e_tests
# TCK 子集
python3 tests/tck/run_tck.py --server-bin build/release/eugraph-server \
  --tck-bin build/release/tests/tck/tck_tests \
  --features tests/tck/features-eugraph/show-indexes.feature \
  --data-dir /home/dodo/.cache/tck-index --port 19999
```

未执行/未覆盖：

* 全量 TCK 与 CI required checks（由 CI 门禁承担；本地只跑 index 相关 feature）；
* sf1 规模（L3）压测；本 PR 用 4000 行 + 并发写做功能压力；
* UBSan（本机运行库缺失，CI sanitizer job 覆盖）；
* `property-ownership/03-index.feature` 的 8 个失败是既有属性所有权/歧义属性断言，与异步 DDL 无关。

## R1/R2/R3/R5 修复验证（2026-11）

| 项 | 用例 / 证据 |
|---|---|
| R1 事务准入 | `IndexWriteApplierTest.AdmittedTransactionContinuesAfterGateCloses`：同一事务拿到准入后关闸，第二条索引写仍走 delta；`UnadmittedWriterWaitsForGateReopen` 等到 >1s 后重开仍成功；`NewIndexDuringAdmittedTransactionFailsFastWithoutDeadlock` 防死锁 |
| R2 批量 pin | `IndexE2ETest.BuildWaitsForBatchTransactionStartedBeforeCreate`：批量事务先 pin 旧 schema，构建等待其提交后仍扫到数据；`BatchTransactionDuringScanWritesDelta`：构建中批量事务预取准入写 delta，PUBLIC 后查询命中；`LoaderE2ETest.BatchInsertDuringOnlineIndexBuildIsMaintained`：真实 GraphService batch + 异步 CREATE INDEX |
| R5 tracker | `IndexE2ETest.IndexDdlWithoutTrackerFailsBeforeBuilding`：DDL 无 tracker 直接报错，不创建半成品索引 |
| R3 | `EUGRAPH_INDEX_PIN_WAIT_TIMEOUT_MS`（0=无限）+ 超时 ERROR/日志；默认无限，避免长事务被误杀 |
| 本地验证 | index_e2e 82/82、applier 14/14、runtime 25/25、loader 43/43、query_executor 673/673、catalog 25/25、rpc 29/29；ASan e2e 82/82、applier 14/14 |
