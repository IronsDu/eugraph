# TCK 测试执行指南

## 概述

EuGraph 使用 [openCypher TCK](https://github.com/opencypher/openCypher/tree/master/tck)（Technology Compatibility Kit）作为 Cypher 查询语言的兼容性测试套件。TCK 基于 Cucumber BDD 框架，使用 `.feature` 文件描述测试场景。EuGraph 自己的扩展场景放在 `tests/tck/features-eugraph/`：`correlated-endpoint-and-size-list-regressions.feature`（相关端点/批量精确 seek/集合探针/目标标签过滤成本模型/`size([...])` 列表元素拓扑化，期望值以 neo4j SF1 为准）、`multi-label-strong-mode.feature`
覆盖多标签 scoped cast 与强属性访问；`vertex-summary.feature` 覆盖顶点摘要缓存的正确性
（CREATE 写入、SET/REMOVE/标签增删/DELETE 失效、大属性回退、MERGE、多行重复顶点、null）；
`path/self-loop-path.feature` 覆盖自环路径的节点/关系序列（`length`/`nodes`/`relationships`
以及 Bolt 路径结构）；`string-index-boundaries.feature` 覆盖字符串索引的精确等值、前缀查询、
复合索引 leading prefix，以及索引扫描返回多个字符串列时的值正确性；
`vertex-summary-invalidation.feature` 覆盖同一事务/跨事务属性更新、属性删除、标签增删后
摘要缓存失效的 Cypher 可见结果。

## 构建

```bash
cmake --build build --target tck_tests
```

依赖 `eugraph-server` 和 `ccr_ext`（cucumber-cpp 运行时）会自动构建。

## 对照测试约定（neo4j）

新增/修改 TCK 场景时，期望值**先在本机 neo4j 上用同一组 `CREATE` + 查询跑出来**，再写进 feature；
不要凭推断填表。共享节点必须先 `CREATE` 一次、后续 `MATCH` 复用——两次 `CREATE` 相同
`id`/属性会得到两个不同节点（本轮就因此得到过 `matched=0` 的假结果）。

## 运行 TCK 测试

### 基本用法

```bash
python3 tests/tck/run_tck.py \
  --server-bin build/eugraph-server \
  --tck-bin build/tests/tck/tck_tests \
  --features third_party/openCypher/tck/features \
  --features tests/tck/features-eugraph \
  --timeout 60
```

脚本会自动：
1. 清理并启动 `eugraph-server`（默认端口 9999）
2. 等待 server TCP 就绪
3. 运行 `tck_tests` 执行所有 `.feature` 场景
4. 停止 server 并清理临时数据

> **全量 TCK 是 PR 合并门禁，不是开发循环工具。** 本地迭代只跑受影响 feature 子集；
> PR 的 required check `Address Sanitizer (TCK)` 会跑全量。L3 前冻结代码；长任务后台运行。
>
> `--features` 必须显式传入：不传时默认 `features` 目录通常不存在，脚本会静默跑 0 场景（#304）。
> 用 `--data-dir <磁盘目录>`（不要 tmpfs）、`--report` 和 `--step-results-file` 把产物写到指定位置，
> 避免 `tck-step-results.json` 落在仓库根目录。

> TCK feature 文件来自 git submodule `third_party/openCypher`（即 [openCypher/openCypher](https://github.com/opencypher/openCypher) 仓库的 `tck/features/` 目录）。更新 TCK 用例：
> ```bash
> git submodule update --remote third_party/openCypher
> ```

### 运行指定 feature 文件

```bash
python3 tests/tck/run_tck.py \
  --server-bin build/eugraph-server \
  --tck-bin build/tests/tck/tck_tests \
  --features third_party/openCypher/tck/features/clauses/call/Call1.feature
```

### 生成详细分类报告

`run_tck.py` 生成的 `--report` 是简要汇总。生成**按类别（表达式、子句、useCases）详细分类**的报告使用 `scripts/gen_tck_report.py`：

```bash
# 先运行 TCK 得到 step-results JSON
python3 tests/tck/run_tck.py \
  --server-bin build/eugraph-server \
  --tck-bin build/tests/tck/tck_tests \
  --features build/tests/tck/features \
  --step-results-file build/tck-step-results.json

# 再生成详细分类报告
python3 scripts/gen_tck_report.py \
  --step-results build/tck-step-results.json \
  --features build/tests/tck/features \
  --output docs/tests/tck-results.md
```

生成的报告包含：表达式类（Literals、List、Aggregation 等 18 项）、子句类（Match、WITH、MERGE、Create 等 16 项）、useCases、与 DPL 基线对比。

### 生成报告并对比基线

```bash
python3 tests/tck/run_tck.py \
  --server-bin build/eugraph-server \
  --tck-bin build/tests/tck/tck_tests \
  --report build/tck-report.md \
  --baseline-report baseline/tck-report.md \
  --step-results-file build/tck-step-results.json \
  --step-baseline-file baseline/tck-step-results.json
```

## CLI 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server-bin` | （必填） | eugraph-server 二进制路径 |
| `--tck-bin` | （必填） | tck_tests 二进制路径 |
| `--port` | `9999` | Server 端口 |
| `--host` | `127.0.0.1` | Server 绑定地址 |
| `--data-dir` | `/tmp/eugraph_tck_data` | WiredTiger 数据目录 |
| `--features` | `features` | Feature 文件或目录 |
| `--timeout` | `30` | Server 启动超时（秒） |
| `--keep-data` | （不保留） | 测试后保留数据目录 |
| `--report` | （不写入） | Markdown 报告输出路径 |
| `--baseline-report` | （不比较） | 基线报告路径（用于指标对比） |
| `--step-results-file` | （不写入） | Step 级结果 JSON 路径（由 tck_tests 生成） |
| `--step-baseline-file` | （不比较） | 基线 step 结果 JSON 路径（用于回归检测） |

`--` 之后的额外参数会透传给 `tck_tests`。

## 环境变量

| 变量 | 说明 |
|------|------|
| `EUGRAPH_HOST` | Server 地址（被 `--host` 覆盖） |
| `EUGRAPH_PORT` | Server 端口（被 `--port` 覆盖） |
| `TCK_STEP_RESULTS_PATH` | Step 结果 JSON 写入路径（tck_tests 读取） |
| `TCK_BASELINE_REPORT_PATH` | 基线 Markdown 报告路径（run_tck.py 读取） |
| `TCK_STEP_BASELINE_PATH` | 基线 Step 结果 JSON 路径（run_tck.py 读取） |

`run_tck.py` 会自动将 `--step-results-file` 的值设为 `TCK_STEP_RESULTS_PATH` 传递给 `tck_tests`。

## 输出文件

### Markdown 报告（`--report`）

包含：
- Scenario / Step 通过/失败/跳过统计
- AST 跳过原因分类
- 与基线报告的指标对比（含 pass rate 变化）
- Step 级回归检测（PASSED → FAILED 的步骤列表）

### Step 结果 JSON（`tck-step-results.json`）

由 `tck_tests` 在 `HOOK_AFTER_ALL` 中写入，每个 step 一条记录：

```json
[
  {
    "scenario": "[1] Standalone call to procedure that takes no arguments and yields no results",
    "index": 0,
    "step": "an empty graph",
    "status": "PASSED"
  }
]
```

字段说明：

| 字段 | 说明 |
|------|------|
| `scenario` | 场景名称（来自 feature 文件） |
| `index` | Step 在场景中的序号（从 0 开始） |
| `step` | 步骤描述文本 |
| `status` | `"PASSED"` 或 `"FAILED"` |

### Server 日志

Server 输出写入 `/tmp/eugraph_tck_server.log`。如果 server 崩溃，`run_tck.py` 会打印末尾 100 行。

## 测试结果标记

| 标记 | 含义 |
|------|------|
| ✔ (passed) | 场景/步骤通过 |
| ■ (undefined) | 缺少 Gherkin 步骤定义 |
| ↷ (skipped) | 前置步骤失败，跳过后续步骤 |
| ✗ (failed) | 断言失败或查询错误 |

## 终端摘要

`tck_tests` 在 `HOOK_AFTER_ALL` 中打印摘要，包含每个场景的名称和通过/失败/跳过状态：

```
[TCK] ==================== SUMMARY ====================
[TCK] Total: 100  |  Passed: 80  |  Failed: 15  |  Skipped: 5
[TCK] === PASSED SCENARIOS ===
[TCK]   PASS: [1] Standalone call to procedure...
[TCK] === FAILED SCENARIOS ===
[TCK]   FAIL: [42] Creating a node with a list property...
```

## 相关文件

| 文件 | 说明 |
|------|------|
| `tests/tck/run_tck.py` | 测试启动脚本（server 生命周期管理 + 报告生成） |
| `tests/tck/tck_steps.cpp` | Cucumber 步骤定义 + Hooks（含场景名收集） |
| `tests/tck/tck_context.cpp` | TCK 上下文（RPC 客户端、AST 跳过检测、结果比对） |
| `tests/tck/tck_context.hpp` | TCK 上下文头文件 |
| `tests/tck/tck_types.hpp` | 数据类型定义（SideEffects, TckCell 等） |
| `tests/tck/CMakeLists.txt` | 构建配置（ccr_ext ExternalProject + CTest 注册） |
| `scripts/gen_tck_report.py` | 生成详细分类 TCK 报告（按表达式/子句/useCases 分类） |
| `scripts/compare_tck.py` | 独立脚本：直接对比两个 cucumber 文本报告 |
| `third_party/openCypher/tck/features/` | openCypher TCK feature 文件（git submodule） |
| `docs/tests/tck-results.md` | 测试结果分类报告 |

## 添加我们自己的 TCK 用例

上游 openCypher TCK 在子模块 `third_party/openCypher` 里，不能改；我们自己的用例放在
**`tests/tck/features-eugraph/`**（入库，与上游并列）。运行时会同时喂给 cucumber：

```
tck_tests  <repo>/third_party/openCypher/tck/features  <repo>/tests/tck/features-eugraph
```

（`tests/tck/CMakeLists.txt` 里就是这两个 `--features`；`ctest -R tck_tests` 与 CI 都走这一条，
所以我们的用例自动进同一份报告、同一套 step 基线与 `--fail-on-regression` 回归保护。）

只跑我们自己的：

```bash
python3 tests/tck/run_tck.py \
  --server-bin build/debug/eugraph-server \
  --tck-bin build/debug/tests/tck/tck_tests \
  --features tests/tck/features-eugraph \
  --report /tmp/eugraph-tck.md
```

`--features` 可以重复（runner 会把所有目录按顺序传给 cucumber），也接受单个 `.feature` 文件。

写用例时：
* 用上游 TCK 已有的 step（`Given an empty graph` / `having executed` / `executing query` /
  `the result should be, in any order:` / `in order:` / `no side effects`），这些在
  `tests/tck/tck_steps.cpp` 里实现；
* 期望值里凡是**渲染形式不确定**的（扩展年份、带秒/不带秒、时区后缀），在查询里用
  `toString(...)` 投影成字符串再断言，避免依赖客户端/服务端两种渲染的差异；
* 需要上游没有的断言（例如"应有 N 个点/M 条边"、"应报某类错误"）时，在
  `tests/tck/tck_steps.cpp` 里加 step，并在这里补一行说明；
* 我们的用例和上游用例共用报告与基线：新增用例不会触发 `--fail-on-regression`
  （只有"基线里 PASSED、现在 FAILED/SKIPPED"才算回归），但**改动已有用例的期望值**会。

### 两个写用例时会踩的坑（实测）

1. **`CALL` 会被当作"不支持语法"跳过。** `TckContext::isQuerySupported()` 里
   `hasUnsupportedClause()` 对 `ast::CallClause` **无条件返回 true**（日志会打
   `[TCK] skipping: CALL`），于是整个场景被跳过、`Then` 断言永远不执行——报告里它仍可能算作
   Failed，看起来像"查询返回了空结果"，很容易误判成引擎缺陷。
   **因此 `.feature` 里不要用 `CALL` 过程**；要验证过程请写 gtest
   （`tests/test_query_executor.cpp` 的 `Procedure*` 用例）。Schema 查询改用 `DESCRIBE` 家族。

2. **`ensureTypesForQuery()` 用正则 `:([A-Za-z_]\w*)` 扫整条查询自动建 label。**
   它会把 `{prop: value}` 里的 `:value`（**冒号后紧跟标识符**）当成标签名去 `createLabel`。
   写属性映射时**在冒号后留一个空格**（`{nickname: 'solo'}`）即可避开；`{nickname:'solo'}` 会多建一个
   叫 `nickname` 的空标签，并污染 `no side effects` 快照。

3. **`isQuerySupported()` 里曾用正则整条跳过 `FOREACH` / `LOAD CSV`。** 这是当年这两者没有 AST
   节点时的兜底。FOREACH 现在有 `ast::ForeachClause` 了，所以那条正则已移除 —— 留着的话
   `.feature` 里的 FOREACH 场景会被**静默跳过**（步骤数变少），断言从不执行，只表现为
   "结果列是空的"这类假失败。新增语法支持时，除了 `hasUnsupportedClause()` 要认识新节点，
   也检查一下这个正则兜底列表。`LOAD CSV` 仍未支持，保留跳过。

4. **`the side effects should be:` 是"全量比对"**：表里没写的指标按 0 比较，而引擎的 diff 还会统计
   `+labels`（新增的标签名）与 `+properties`（按 key 增删）。所以 `CREATE (:T {v: 1})` 只写
   `| +nodes | 1 |` 会因为实际 `+labels=1`、`+properties=1` 而失败。要么把全部指标写全，
   要么改用结果断言（`MATCH ... RETURN count(*)`）；`no side effects` 只断言全零，适合空列表/null 这类 no-op 场景。

另外，**新建的图一定带匿名标签 `__anon__`**（`GraphManager` 建图时安装，用于承载无标签节点的属性），
所以 `DESCRIBE LABELS` 在空图上就有 1 行 —— 断言空图标签列表时可以直接写全量期望值。
裸 `CREATE` 不登记 label 字段：未声明的属性写入弱空间 `__anon__`（见 `cypher-syntax.md` 8.4 与
[property-ownership.md](../features/property-ownership.md)）。schema 用例断言 `DESCRIBE LABEL <label>` 时
应预期空结果，断言弱键请用 `DESCRIBE LABEL __anon__`。

### ⚠️ `tck_tests` 偶发失败：WiredTiger 在 teardown 时 panic（与用例无关）

全量 `tck_tests` 偶尔会以 `Failed` 收场，但**用例本身全过**：

```
[TCK] Total: 3928  |  Passed: 3928  |  Failed: 0  |  Skipped: 0
[run_tck] Server killed by signal 6
[run_tck] WARNING: Server crashed (signal 6)
[run_tck] Failing due to server crash
```

**判据**：看 `[TCK] Total:` 那行的 `Failed` 是不是 0。是 0 就说明引擎行为没问题，失败来自
服务端进程在收尾阶段被 abort。

**根因**（`coredumpctl info <pid>` 看栈）：崩在 WiredTiger 内部，不是我们的代码：

```
__wt_abort
__wt_panic_func      ← WT 主动 panic
__log_server         ← WT 的日志服务线程
```

即 WT 日志子系统 panic 后 `abort()`，与查询执行无关（此时 TCK 已跑完、最后一张图也已 drop）。

**这是长期存在的偶发问题，不是某次改动引入**：本机 `coredumpctl list` 里 `eugraph-server`
有 375 条 coredump，横跨 2026-05 ~ 2026-09 且每月都有。同一份二进制重跑通常就过
（实测：首次全量 1168 项里仅 `tck_tests` 因它失败，重跑 1168/1168 全过）。

所以遇到它时：**先确认 `Failed: 0`，再重跑一次**，不要把它当成引擎缺陷去改查询逻辑。
要真正定位得单独查 WT 日志子系统（`--wt-txn-sync` / 日志文件清理路径），属独立议题。


