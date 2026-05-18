# TCK 测试执行指南

## 概述

EuGraph 使用 [openCypher TCK](https://github.com/opencypher/openCypher/tree/master/tck)（Technology Compatibility Kit）作为 Cypher 查询语言的兼容性测试套件。TCK 基于 Cucumber BDD 框架，使用 `.feature` 文件描述测试场景。

## 构建要求

```bash
cmake --build build --target tck_tests
```

依赖：`eugraph-server` 和 `ccr_ext`（cucumber-cpp 运行时）会自动构建。

## 运行 TCK 测试

```bash
python3 tests/tck/run_tck.py \
  --server-bin build/eugraph-server \
  --tck-bin build/tests/tck/tck_tests \
  --features tests/tck/features \
  --timeout 60
```

脚本会自动：
1. 启动 `eugraph-server`（端口 9999）
2. 运行 `tck_tests` 执行所有 `.feature` 场景
3. 停止 server 并清理临时数据

## 运行指定 feature 文件

```bash
python3 tests/tck/run_tck.py \
  --server-bin build/eugraph-server \
  --tck-bin build/tests/tck/tck_tests \
  -- features/opencypher_tck/clauses/create/Create1.feature
```

## 测试结果说明

| 标记 | 含义 |
|------|------|
| ✔ (passed) | 场景通过 |
| ■ (undefined) | 缺少 Gherkin 步骤定义 |
| ↷ (skipped) | 前置步骤失败，跳过后续步骤 |
| ✗ (failed) | 断言失败或查询错误 |

## 相关文件

| 文件 | 说明 |
|------|------|
| `tests/tck/run_tck.py` | 测试启动脚本 |
| `tests/tck/tck_steps.cpp` | Cucumber 步骤定义 |
| `tests/tck/tck_context.cpp` | TCK 上下文（含 AST 跳过逻辑） |
| `tests/tck/features/` | openCypher TCK feature 文件 |
| `docs/tests/tck-results.md` | 测试结果分类报告 |
