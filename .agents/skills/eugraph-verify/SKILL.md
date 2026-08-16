---
name: eugraph-verify
description: 当用户要求编译、构建、运行测试或提交前验证 EuGraph 时使用（关键词：verify、build、test、ctest、编译、测试、验证、跑测试）。根据改动范围执行格式检查、cmake 构建、相关测试和全量 ctest；涉及 grammar/thrift/TCK 时执行对应专项验证。命令事实来源与兜底命令以仓库根目录 AGENTS.md 为准。
---

# EuGraph 构建与测试验证

## 何时使用

- 用户说“验证一下”“跑一下测试”“提交前检查”“编译看看”；
- 完成编码后需要执行根目录 `AGENTS.md` 零-二 / 零-三第 4 步。

## 第 1 步：确定验证范围

- 用户说“完整验证”或“提交前检查” → 执行下方默认流程；
- 用户说“只编译” → 只执行构建步骤；
- 用户说“只跑某个测试” → 只执行该测试并在报告中说明未跑全量；
- 用户没有明确范围时，默认执行完整验证。

不要擅自跳过失败项，也不要在用户只要求局部验证时借机跑大量无关命令。

## 第 2 步：查看工作区

```sh
git status --short --branch
git diff --name-only
git diff --cached --name-only
```

根据改动文件判断涉及的模块和最小验证集。纯 Markdown/规则变更不需要编译测试，按开发者确认的 N/A 处理。

## 第 3 步：格式化

改动涉及 `src/` 或 `tests/` 的 C++ 文件时：

```sh
./scripts/check-format.sh --fix
./scripts/check-format.sh
```

纯文档变更可跳过。

## 第 4 步：构建

```sh
# 首次构建或 CMake 配置变更时先执行
cmake --preset debug

cmake --build --preset debug
```

开发者指定其他 preset 时使用指定项。构建失败时停止并修复，不要带着编译错误继续跑测试。

## 第 5 步：测试

1. 先运行与本次改动直接相关的测试二进制，例如：

   ```sh
   ./build/debug/query_executor_tests --gtest_filter='*<RelatedTest>*'
   ```

   把二进制名和 filter 替换为实际相关项；不确定哪个二进制最相关时，根据 `git diff` 涉及的模块选择，必要时询问开发者。

2. 完整验证时再运行：

   ```sh
   ctest --preset debug --output-on-failure
   ```

3. 测试失败：
   - 只修复与当前任务相关的代码；
   - 不得删除、弱化或绕过测试用例；
   - 修复后重跑失败项，必要时再跑全量；
   - 无法自行修复时停止并说明。

## 第 6 步：专项验证

- 修改 `grammar/*.g4`：运行 `./scripts/gen-grammar.sh`，并按 `docs/build/build-guide.md` 校验生成结果。
- 修改 `proto/eugraph.thrift`：按 `docs/build/build-guide.md` 重新生成 gen-cpp2 代码，并校验 include 路径。
- 查询语义 / Bolt / KV 编码变更：按 `docs/tests/tck-guide.md` 运行 TCK；不得在未取得开发者确认时更新 TCK 基线。

## 第 7 步：报告

报告必须包含：

- 实际执行的命令；
- 格式化 / 构建 / 相关测试 / 全量测试的退出状态；
- 失败项和已采取的修复动作；
- 哪些步骤按用户要求未执行。

禁止声称“应该通过”“预计通过”或编造输出。
