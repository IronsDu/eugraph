---
name: eugraph-verify
description: 当用户要求编译、构建、运行测试或提交前验证 EuGraph 时使用（关键词：verify、build、test、ctest、编译、测试、验证、跑测试）。按 L0–L3 验证阶梯选择范围：格式、构建、相关测试、受影响 TCK 子集、全量 ctest/TCK；L3 优先交给 CI，长任务后台运行并报 ETA。命令事实来源与兜底命令以仓库根目录 AGENTS.md 为准。
---

# EuGraph 构建与测试验证

## 何时使用

- 用户说“验证一下”“跑一下测试”“提交前检查”“编译看看”；
- 完成编码后按 `AGENTS.md`“一、交付契约 / 三、验证阶梯”执行。

## 验证阶梯

| 级别 | 内容 | 耗时 | 何时 |
|---|---|---|---|
| L0 | 格式化 + 构建目标 + fail-before/定向单测 + 单条查询 | < 2 min | 每次小改动 |
| L1 | 相关测试二进制 + 1–3 条代表性查询对 neo4j + perf 微 A/B（3–5 轮） | 2–10 min | 每个完整增量 |
| L2 | 全量 `query_executor_tests` + `optimizer_tests` + 受影响 TCK 子集 + LDBC 21 条语义对照 | 10–20 min | 功能完成 / 开 PR 前 |
| L3 | 全量 TCK + ASan/UBSan | 30–60 min | PR ready / merge 前，优先 CI |

> **全量 TCK 是门禁，不是开发循环工具。** 迭代期只跑受影响 feature 子集；L3 优先交给
> PR 的 required checks（含 `Address Sanitizer (TCK)`），本地只在 CI 不可用/排障时跑全量。

## 第 1 步：确定级别与范围

- 用户说“完整验证/提交前检查” → L2（功能完成）或 L3（PR ready，优先 CI）；
- 用户说“只编译” → 只构建；
- 用户说“只跑某个测试” → 只跑该测试，并在报告中说明未跑全量；
- 未说明时按改动面选择**最小充分**级别，不要擅自跑无关的全量。

## 第 2 步：工作区与自审

```bash
git status --short --branch
git diff --name-only
git diff --cached --name-only
```

- 根据改动文件判断模块与最小验证集；
- 纯 Markdown/规则变更不走构建测试，报告中写明 N/A；
- 提交前自审：调试残留、越界改动、未跟踪产物、只提交预期文件。

## 第 3 步：格式化

涉及 `src/` 或 `tests/` 的 C++ 文件：

```bash
./scripts/check-format.sh --fix
./scripts/check-format.sh
```

纯文档可跳过。

## 第 4 步：构建

```bash
cmake --preset debug            # 首次或 CMake 配置变更
cmake --build --preset debug
```

开发者指定 preset 时使用指定项。**构建失败时停止并修复**，不要带编译错误继续测试。

## 第 5 步：测试

1. **L0/L1**：先跑相关测试二进制与代表性查询：

   ```bash
   ./build/debug/query_executor_tests --gtest_filter='*<RelatedTest>*'
   ```

   不确定相关二进制时按 `git diff` 模块选择；不要直接上全量。

2. **L2**：全量单测 + 受影响 TCK 子集：

   ```bash
   ./build/debug/query_executor_tests
   ./build/debug/optimizer_tests
   ```

   TCK 子集按改动面选择（示例）：

   | 改动 | feature |
   |---|---|
   | 列表/模式推导 | `expressions/list`、`expressions/pattern` |
   | EXISTS / 相关子查询 | `expressions/existentialSubqueries` |
   | CREATE / labels | `expressions/graph`、`clauses/create` |
   | Expand / 匹配计划 | `clauses/match` |
   | eugraph 扩展 | `tests/tck/features-eugraph` |

   ```bash
   python3 tests/tck/run_tck.py \
     --server-bin build/debug/eugraph-server \
     --tck-bin build/debug/tests/tck/tck_tests \
     --data-dir <磁盘目录> \
     --features third_party/openCypher/tck/features/expressions/list \
     --features tests/tck/features-eugraph
   ```

   **必须显式传 `--features`**：不传时默认 `features` 目录不存在，会静默跑 0 场景（#304）。

3. **L2 全量单测**（需要时）：

   ```bash
   ctest --preset debug --output-on-failure
   ```

4. 测试失败：
   - 只修复与当前任务相关的问题；
   - 不得删除/弱化/绕过测试；
   - 修复后重跑失败项，必要时再跑全量；
   - 无法自行修复时停止并说明。

## 第 6 步：L3（全量 TCK / sanitizer）

- 优先交给 CI；本地只在 CI 不可用或排障时执行；
- 跑前冻结代码（commit/停止改动），确认 L1/L2 绿、方案已认可；
- 数据目录放磁盘（不得 tmpfs），ASan 构建低并行（如 `-j 2`）；
- **预计 > 5 min**：先告知 ETA，后台运行，继续做文档/PR 描述等独立工作；
- 前提变了（方向/结果有问题）立即 kill，不“等它跑完”。

```bash
python3 tests/tck/run_tck.py \
  --server-bin <asan-server> \
  --tck-bin <asan-tck-tests> \
  --data-dir <磁盘目录> \
  --features third_party/openCypher/tck/features \
  --features tests/tck/features-eugraph
```

## 第 7 步：专项验证

- 修改 `grammar/*.g4`：`./scripts/gen-grammar.sh`，按 [docs/build/build-guide.md](../../../docs/build/build-guide.md) 校验生成结果。
- 修改 `proto/eugraph.thrift`：按 build-guide 重新生成 gen-cpp2，校验 include 路径。
- 查询语义 / Bolt / KV 编码：按 [docs/tests/tck-guide.md](../../../docs/tests/tck-guide.md) 跑 TCK；**不得擅自更新基线**。
- 语义改动优先用 neo4j 对照：`python3 scripts/verify_ldbc_results.py ...`。

## 第 8 步：报告（证据契约）

报告必须包含：

- 实际执行的命令与**退出码**；
- 关键输出（通过/失败数量、失败项）；
- 失败项和已采取的修复动作；
- 哪些步骤按用户要求/级别未执行（写明原因）；
- 性能数字注明轮数、warmup、机器状态。

禁止声称“应该通过”“预计通过”或编造输出。

## 测试环境约定

- 测试数据目录**不得放 tmpfs**（`/tmp` 常为 tmpfs）：用 `TMPDIR=<磁盘目录>` 或显式数据目录，留数 GB；
- ASan 低并行；同一服务端重复执行时用唯一库名/前缀隔离；
- 长任务后台化，避免让开发者等待。
