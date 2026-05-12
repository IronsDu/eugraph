# 正确性测试框架设计

> 基于 openCypher TCK 的数据驱动 Cypher 正确性回归测试框架。

## 一、架构

```
openCypher TCK (.feature)           ← 上游 Apache 2.0
        │
        ▼ scripts/extract_tck.py
        │
  ┌─────┴──────────────────────────────┐
  │ tests/correctness/*.json           │  ← 数据驱动用例（结构见 §三）
  │ tests/correctness_registration.cpp │  ← 自动生成，69 个 GTest 套件注册
  │ tests/correctness/README.md        │  ← 人类可读用例汇总
  └────────────────────────────────────┘
        │
        ▼ tests/test_correctness.cpp + tests/json_parser.h
        │
  GTest TEST_P（每场景独立报告）
        │  SetUp: 新建 WiredTiger 实例
        │  执行 setup 语句 → 执行查询 → 对比结果
        │  TearDown: 清理临时目录
        ▼
  CTest (ctest --preset=debug)
```

## 二、文件说明

| 文件 | 角色 | 是否提交 |
|------|------|:------:|
| `scripts/extract_tck.py` | 从 TCK .feature 提取并转换为 JSON + 生成 C++ 注册代码 | 是 |
| `tests/json_parser.h` | 极简 JSON 解析器（无外部依赖，仅处理本项目 JSON 格式） | 是 |
| `tests/test_correctness.cpp` | GTest 参数化测试夹具（CorrectnessTest + TEST_P(Run)） | 是 |
| `tests/correctness_registration.cpp` | 自动生成的 69 个 `INSTANTIATE_TEST_SUITE_P` 调用 | 是 |
| `tests/correctness/*.json` | 按 TCK 目录组织的测试用例（84 文件，1153 场景） | 是 |

## 三、用例 JSON 格式

### 3.1 普通查询场景

```jsonc
{
  "id": "Null1 - IS NULL validation-[4] A literal null IS null",
  "description": "[4] A literal null IS null",
  "setup": [],                          // 数据准备语句（可为空）
  "query": "RETURN null IS NULL AS value",
  "expect_columns": ["value"],          // 期望列名
  "expect_rows": [["true"]],            // 期望行数据（全字符串）
  "ordered": false,                     // 结果是否需保序
  "requires": [],                       // 依赖的功能特性
  "tags": []
}
```

### 3.2 带数据准备的查询场景

```jsonc
{
  "id": "Aggregation1 - Count-[1] Count only non-null values",
  "setup": [
    "CREATE ({name: 'a', num: 33})",    // 多条 setup 按序执行
    "CREATE ({name: 'a'})",
    "CREATE ({name: 'b', num: 42})"
  ],
  "query": "MATCH (n)\nRETURN n.name, count(n.num)",
  "expect_columns": ["n.name", "count(n.num)"],
  "expect_rows": [["'a'", "1"], ["'b'", "1"]]
}
```

### 3.3 错误场景

```jsonc
{
  "id": "Comparison1 - Equality-[17] Failing when comparing...",
  "is_error": true,
  "error_type": "SyntaxError",          // 或 TypeError
  "error_contains": "UndefinedVariable",// 错误信息应包含的文本
  "setup": [],
  "query": "MATCH (s) WHERE s.name = undefinedVariable ... RETURN s"
}
```

### 3.4 参数化展开

TCK 的 Scenario Outline + Examples 表格会被展开为独立场景，参数值替换到 query/expect 中。例如：

```gherkin
Scenario Outline: [6] Comparing lists to lists
  When executing query:
    """
    RETURN <lhs> = <rhs> AS result
    """
  Then the result should be, in any order:
    | result   |
    | <result> |

  Examples:
    | lhs    | rhs  | result |
    | [1, 2] | [1]  | false  |
    | [null] | [1]  | null   |
    ...共 6 行
```

展开后生成 6 个独立 JSON 场景，每个场景的参数 `<lhs>`, `<rhs>`, `<result>` 已替换为具体值。

## 四、提取与转换流程

### 4.1 命令

```bash
# 一次性：从 TCK 仓库提取 + 生成 JSON + 生成 C++ 注册 + 生成 markdown 汇总
python3 scripts/extract_tck.py \
    --tck /tmp/openCypher/tck \
    --out tests/correctness \
    --summary \
    --gen-cpp tests/correctness_registration.cpp
```

### 4.2 功能能力分类

`extract_tck.py` 中的 `FEATURE_CAPABILITY` 字典控制每个 feature 的处理方式：

| 标记 | 含义 | 示例 |
|------|------|------|
| `full` | 全量提取，不加 note | Null, Literals, Precedence |
| `partial` | 提取但标注 `may need manual adjustment` | Comparison, Boolean, Match, Return |
| `skip` | 完全跳过（功能未实现） | String, List, TypeConversion, Unwind |

当项目实现新功能时，将对应 feature 从 `skip` 改为 `partial` 或 `full`，重新运行提取脚本即可导入新用例。

### 4.3 覆盖统计

| 指标 | 数值 |
|------|------|
| TCK 总场景数（展开后） | 3897 |
| 已提取 | 1153 (29.6%) |
| 跳过（capability=skip） | 2744 (70.4%) |
| 可提取范围内的覆盖率 | 100%（零遗漏） |

## 五、测试跑步器

### 5.1 测试夹具

```cpp
class CorrectnessTest : public ::testing::TestWithParam<CorrectnessCase> {
    // SetUp:    创建独立 WiredTiger 数据目录 + QueryExecutor
    // TearDown: 清理临时目录
    // execSync: folly::coro::blockingWait + prepareStream → 同步结果
};
```

### 5.2 测试执行

```cpp
TEST_P(CorrectnessTest, Run) {
    // 1. 执行 setup 语句（逐条，失败则 GTEST_SKIP）
    // 2. 执行被测查询
    // 3. 分类型验证：
    //    - is_error → 检查 result.error 包含 error_contains
    //    - 普通场景 → 比较 column 数量、row 数量、逐列值
}
```

每个测试场景**独立 WiredTiger 环境**（`/tmp/eugraph_correctness_<pid>_<n>/`），避免用例间数据污染。

### 5.3 自动注册

`correctness_registration.cpp`（由 `--gen-cpp` 生成）：

```cpp
// Match1 (86 scenarios)
static auto __cases_clauses_match_Match1 = LoadCasesFromJSON(
    std::string(CORRECTNESS_JSON_DIR) + "/clauses/match/Match1.json");
INSTANTIATE_TEST_SUITE_P(
    clauses_match_Match1, CorrectnessTest,
    ::testing::ValuesIn(__cases_clauses_match_Match1));
// ... 共 69 个 suite
```

`CORRECTNESS_JSON_DIR` 由 CMake 编译定义为源码树中的 `tests/correctness/` 绝对路径。

## 六、运行

```bash
# 构建
cmake --build . --target correctness_tests

# 全量运行
./correctness_tests

# 按 TCK feature 过滤
./correctness_tests --gtest_filter="*Null1*"

# 按场景过滤
./correctness_tests --gtest_filter="*Aggregation1*Count_only*"

# 通过 CTest（CI）
ctest --preset=debug -R correctness_tests
```

## 七、扩展指南

### 导入新的 TCK 功能类别

1. 在 `FEATURE_CAPABILITY` 中将对应 feature 从 `"skip"` 改为 `"partial"` 或 `"full"`
2. 重新运行提取脚本（同上命令）
3. 提交更新的 JSON 文件和注册代码
4. 构建并运行验证新增用例数

### 添加手写用例

直接创建 JSON 文件，遵循 §三 格式，放入 `tests/correctness/` 对应目录。当前脚本不会覆盖手工文件（因为 TCK 里没有对应 feature）。

### 同步上游 TCK 更新

1. `git clone --depth 1 https://github.com/opencypher/openCypher.git /tmp/openCypher`
2. 重新运行提取脚本
3. `git diff tests/correctness/` 查看新增/变更用例
4. 提交

---

> 相关文档：
> - [方案与实施计划](cypher-correctness-test-plan.md)
> - [openCypher TCK](https://github.com/opencypher/openCypher) (Apache 2.0)
