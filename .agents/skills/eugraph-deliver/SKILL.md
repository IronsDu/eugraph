---
name: eugraph-deliver
description: 当任务需要收尾、提交、推送、创建/更新 PR、生成 PR 标题/描述或创建 Issue 时使用（关键词：提交、commit、push、PR、pull request、issue、收尾、交付、合并前）。自动完成自审、验证、文档同步、commit 折叠、push、PR 标题/描述/label/关联 Issue，并报告 CI 状态；永不 merge/force-push。
---

# EuGraph 交付：commit / push / PR / Issue

## 何时使用

- 用户说“收尾”“提交”“推送”“建 PR”“写 PR 描述”“记个 issue”；
- 功能完成、准备 review/merge 前。

## 前置检查

```bash
git status --short --branch        # 不在 main/develop；只包含预期改动
gh auth status                     # 未登录则先让用户 gh auth login
git log --oneline origin/main..HEAD
```

- 任务分支已存在；当前分支不是 `main`/`develop`；
- 若涉及 T2（兼容面/发布/merge/force-push），先确认；
- 先按 `eugraph-verify` 做 L2（PR 前）；L3 全量 TCK 优先交给 CI。

## 第 1 步：自审 diff

- 无调试残留（`[DEBUG]`、临时日志、注释掉的代码）；
- 无越界文件/无关格式化/未跟踪产物；
- 测试覆盖了变更（fail-before/反向用例）；
- commit 能按逻辑拆分（功能 / 测试 / 文档 / 修复）。

## 第 2 步：验证

按 `eugraph-verify` 执行 L2：

- `query_executor_tests`、`optimizer_tests`；
- 受影响 TCK feature 子集（见 verify skill 的映射）；
- 语义改动对 neo4j；性能改动同二进制交错 A/B；
- 记录命令、退出码、关键输出；失败先修，不带失败提交。

## 第 3 步：文档同步

- 接口/行为/文件组织变化 → 更新对应设计文档；
- 新增设计文档 → 更新 `docs/README.md` 导航/任务路由；
- benchmark 表、known-defects、环境变量表按需更新。

## 第 4 步：折叠 commit

- 按逻辑拆分：`feat` / `fix` / `perf` / `docs` / `refactor` / `chore` + scope；
- 消息写“为什么 + 关键验证”，不是文件清单；
- 不把无关改动塞进同一个 commit。

## 第 5 步：push 任务分支

```bash
git push -u origin "$(git branch --show-current)"
```

- 预授权：任务分支 push 不用问；
- **禁止 force-push**（除非用户明确授权）；
- 不直接推 `main`/`develop`。

## 第 6 步：创建/更新 PR

先查是否已有 PR：

```bash
gh pr list --head "$(git branch --show-current)" --state open
```

- 无 → 创建 **draft**（首次 push），DoD 通过后转 ready：

  ```bash
  gh pr create --base main --draft \
    --title "type(scope): summary" \
    --body-file <pr-body.md>
  gh pr edit <number> --add-label <label> --add-assignee @me
  gh pr ready <number>
  ```

- 有 → 只更新，不重复创建：

  ```bash
  gh pr edit <number> --title "..." --body-file <pr-body.md>
  ```

- 标题：`type(scope): summary`（英文，conventional）；
- 正文：中文，按 `.github/pull_request_template.md`；
- label：`bug` / `performance` / `documentation` / `enhancement`（按需）；
- 关联 Issue（`Closes #N` / `Refs #N`）；
- draft → ready 的时机：DoD 通过、CI required checks 至少已启动。

### PR 正文模板

```markdown
## 概要
一句话 + 关键数字。

## 范围与非目标
改了什么；明确不改什么。

## 证据
- 命令 + 退出码 + 关键输出
- 测试数量（query_executor/optimizer/TCK）
- 语义对照（neo4j / 独立路径）

## 性能（如有）
基线 / 现在 / 变化；测量口径（轮数、warmup、机器状态、结果行一致）。

## 兼容性与风险
外部兼容面、接口变化、默认行为变化、回退方式。

## 未验证 / 已知问题
未跑的测试、已知缺口、后续 Issue。

## Issue
Refs #N

## Checklist
- [ ] 格式/构建/相关测试通过
- [ ] 语义/性能证据完整
- [ ] 文档同步
- [ ] 自审 diff
```

## 第 7 步：越界缺陷建 Issue（只建不修）

发现与当前任务无关的缺陷时，用模板建 Issue，附最小复现、期望/实际、影响面、定位起点：

```bash
gh issue create --title "[scope] ..." --body-file <issue-body.md> --label bug
```

- 不在本任务里顺手修（除非用户要求）；
- 在 PR/commit 中引用 Issue 编号；
- 已知/已否证方向也记录，避免重复。

## 第 8 步：报告 CI 状态

```bash
gh pr checks <number>
gh pr view <number> --json mergeStateStatus,statusCheckRollup
```

- 报告 required checks 的 pass/pending/fail；
- 失败：拉日志（`gh run view <run-id> --log-failed`）定位；能修则修，不能修则报告；
- `mergeStateStatus=BLOCKED` 时说明缺哪项（checks/review）；
- **永不代替用户 merge**；不 auto-merge、不 force-push、不发布，除非明确授权。

## 安全与边界

- 不输出/提交 secrets；
- 不 force-push、不删除分支/数据、不改 CI/ruleset（除非明确确认）；
- 已合并的分支不要复用；新任务从最新 `main` 建分支。
