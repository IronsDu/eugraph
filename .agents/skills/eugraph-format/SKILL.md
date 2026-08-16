---
name: eugraph-format
description: 当用户要求格式化、检查或修复 EuGraph 的 C++ 代码风格时使用（关键词：format、clang-format、格式化、代码风格、check format）。运行 ./scripts/check-format.sh，修复时加 --fix；脚本要求 clang-format 18。命令事实来源与兜底命令以仓库根目录 AGENTS.md 为准。
---

# EuGraph 代码格式化

## 何时使用

- 用户说“格式化一下”“检查代码格式”“修复格式”；
- 提交前需要执行 `AGENTS.md` 零-二中的格式化检查。

## 执行步骤

1. 确认仓库根目录：

   ```sh
   git rev-parse --show-toplevel
   ```

2. 如果准备执行 `--fix`（会修改文件），先遵守根目录 `AGENTS.md` 零-一：不得直接在 `main` / `develop` 上修改文件。

3. 只处理 `src/` 与 `tests/` 下的 C++ 文件。`./scripts/check-format.sh` 已排除 `generated/` 和 `gen-cpp2/`；不要手动扩大范围去格式化无关文件。

4. 按用户意图执行：

   ```sh
   # 需要自动修复时
   ./scripts/check-format.sh --fix

   # 只检查不修改时
   ./scripts/check-format.sh
   ```

5. `--fix` 后必须再次运行检查模式确认通过，并执行：

   ```sh
   git diff --check
   ```

6. 报告实际执行的命令、退出码和被修改的文件。没有真实运行不得声称通过。

## 备注

- 脚本要求 clang-format 18。本地版本不匹配时，若 Docker/Podman 可用会自动走容器；都不可用时会报错，此时停止并询问，不要擅自用其他版本格式化。
- 命令事实来源是根目录 `AGENTS.md`；本 skill 只负责执行流程。
