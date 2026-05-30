# TCK 测试执行指南

## 概述

EuGraph 使用 [openCypher TCK](https://github.com/opencypher/openCypher/tree/master/tck)（Technology Compatibility Kit）作为 Cypher 查询语言的兼容性测试套件。TCK 基于 Cucumber BDD 框架，使用 `.feature` 文件描述测试场景。

## 构建

```bash
cmake --build build --target tck_tests
```

依赖 `eugraph-server` 和 `ccr_ext`（cucumber-cpp 运行时）会自动构建。

## 运行 TCK 测试

### 基本用法

```bash
python3 tests/tck/run_tck.py \
  --server-bin build/eugraph-server \
  --tck-bin build/tests/tck/tck_tests \
  --timeout 60
```

脚本会自动：
1. 清理并启动 `eugraph-server`（默认端口 9999）
2. 等待 server TCP 就绪
3. 运行 `tck_tests` 执行所有 `.feature` 场景
4. 停止 server 并清理临时数据

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
| `scripts/compare_tck.py` | 独立脚本：直接对比两个 cucumber 文本报告 |
| `third_party/openCypher/tck/features/` | openCypher TCK feature 文件（git submodule） |
| `docs/tests/tck-results.md` | 测试结果分类报告 |
