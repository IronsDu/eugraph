# Cypher 正确性测试方案

> 基于 openCypher TCK 用例提取与转换，构建自动化正确性回归测试体系。

---

## 一、背景与动机

当前项目有 489 个 GTest 用例，以 `test_query_executor.cpp`（195 个）为核心的集成测试覆盖基本查询路径。但缺少系统性的正确性回归测试——边界条件（NULL 传播、类型转换容错、空结果集）和复杂组合场景（聚合+过滤+排序）的覆盖不足。

openCypher TCK（Technology Compatibility Kit）是 Neo4j 维护的 Cypher 语义标准测试套件，含 220 个 `.feature` 文件、数千条场景，覆盖表达式求值、类型转换、NULL 语义、聚合、模式匹配、DDL/DML 等。直接运行 TCK 框架（Cucumber/JVM）不现实——C++ 生态缺乏成熟的 Cucumber 实现。但 TCK 的用例结构高度规整（三段式：数据准备→执行查询→对比结果），可以提取为数据驱动的 GTest 测试。

## 二、TCK 用例结构分析

每个 `.feature` 文件由 Apache 2.0 许可，包含若干 Scenario/Scenario Outline：

```gherkin
Scenario: [1] Number-typed integer comparison
  Given an empty graph
  And having executed:                    # 数据准备（CREATE 语句）
    """
    CREATE ({id: 0})
    """
  When executing query:                   # 被测查询
    """
    MATCH (n) WHERE toInteger(n.id) = 0
    RETURN n
    """
  Then the result should be, in any order: # 期望结果（表格）
    | n         |
    | ({id: 0}) |
  And no side effects
```

关键属性：
- **数据类型**：属性值可以区分整数/浮点（`1` vs `1.0`），表格结果完整表示
- **错误场景**：`Then a SyntaxError should be raised at compile time: ...` 或 `Then a TypeError should be raised at runtime: ...`
- **参数化**：Scenario Outline + Examples 表格，类似 GTest 的 `TEST_P`
- **图数据类**：结果可能包含 `(:Label {prop: val})` 格式的节点/关系表示
- **结果顺序**：默认 `in any order`（无序比较）；少数场景要求 `in order`

### 2.1 用例统计

| 类别 | 文件数 | 典型场景 |
|------|--------|---------|
| 表达式-比较 | 7 | `=`/`<>`/`<`/`>` 含 NULL 传播、跨类型比较、NaN |
| 表达式-布尔 | 5 | AND/OR/NOT/XOR、短路求值 |
| 表达式-数学 | 4 | `+`/`-`/`*`/`/`/`%`/`^`、类型提升、除零 |
| 表达式-字符串 | 14 | substring、trim、replace、STARTS WITH 等 |
| 表达式-列表 | 9 | 索引、切片、IN、列表推导式、comprehension |
| 表达式-NULL | 4 | NULL 在三值逻辑中的传播 |
| 表达式-类型转换 | 11 | toBoolean/toInteger/toFloat/toString |
| 表达式-聚合 | 14 | count/avg/sum/min/max/collect/stDev/stDevP/percentile |
| 表达式-字面量 | 3 | 数字、字符串、布尔、NULL 字面量 |
| 表达式-条件 | 4 | CASE WHEN/coalesce |
| 子句-MATCH | 10 | 节点/边匹配、多标签、属性谓词、变长路径 |
| 子句-WHERE | 6 | 过滤组合、标签/属性/路径谓词 |
| 子句-RETURN | 12 | 返回单变量、表达式、DISTINCT、别名、`*` |
| 子句-WITH | 7 | 管道传递、聚合后传递、排序后传递 |
| 子句-ORDER BY | 6 | 升序/降序、多字段、NULL 排序位置 |
| 子句-SKIP/LIMIT | 5 | 分页组合 |
| 子句-UNWIND | 5 | 列表展开 |
| 子句-CREATE | 8 | 节点/边创建、约束违反 |
| 子句-DELETE | 5 | DETACH DELETE、删除后访问 |
| 子句-SET | 5 | 属性设置、标签设置 |
| 子句-REMOVE | 3 | 属性/标签移除 |
| 子句-MERGE | 3 | ON CREATE/ON MATCH |
| 子句-UNION | 3 | UNION/UNION ALL |
| 用例-其他 | 若干 | 三角选择、子图计数、反斜杠转义等 |

### 2.2 项目当前能力映射

基于 `docs/query/syntax/cypher-syntax.md` 的记录：

| TCK 类别 | 已实现可提取 | 仅解析 | 未实现 |
|----------|:----------:|:-----:|:-----:|
| 比较 `=`/`<>`/`<`/`>`/`<=`/`>=` | 全量 | - | - |
| 逻辑 AND/OR/NOT | 全量 | XOR | - |
| IS NULL / IS NOT NULL | 全量 | - | - |
| 算术 `+`/`-`/`*`/`/` | 全量 | `%`/`^` | - |
| 聚合 count/avg/sum/min/max | 全量 | collect/stDev 等 | - |
| MATCH 节点/边/变长 | 已实现部分 | OPTIONAL MATCH | 多 MATCH |
| WHERE 过滤 | 已实现部分 | 路径谓词 | - |
| RETURN/DISTINCT/AS | 已实现部分 | - | - |
| ORDER BY / SKIP / LIMIT | 全量 | - | - |
| 类型转换 toInteger/toFloat 等 | - | - | 未实现 |
| 字符串函数 substring/trim 等 | - | 全量（仅解析） | - |
| 列表操作/IN/comprehension | - | IN 仅解析 | 未实现 |
| UNWIND | - | - | 未实现 |
| CREATE/DELETE/SET | 已实现部分 | - | MERGE |
| CASE/coalesce | - | - | 未实现 |
| UNION | - | - | 未实现 |

**第一优先级（立即可提取）**：比较、布尔、NULL、部分数学、部分聚合、基础 MATCH/WHERE/RETURN/ORDER BY/SKIP/LIMIT。

## 三、方案设计

### 3.1 架构概览

```
openCypher TCK (.feature)        # 上游源：Apache 2.0，随版本更新
        │
        ▼
  extract_tck.py                 # Step 1: 解析 + 筛选 + 转 JSON
        │
        ▼
  tests/correctness/*.json       # 中间表示：版本可控、人工审查
        │
        ▼
  tests/test_correctness.cpp     # Step 2: GTest 参数化跑步器
        │
        ▼
  CTest (ctest --preset=debug)   # CI 集成，与现有流程一致
```

### 3.2 中间 JSON 格式

```jsonc
{
  "source": "tck/comparison/Comparison1.feature",
  "source_version": "openCypher 2024.2",
  "scenarios": [
    {
      "id": "Comparison1-[1]",
      "description": "Number-typed integer comparison",
      "setup": [
        "CREATE ({id: 0})"
      ],
      "query": "MATCH (n) WHERE toInteger(n.id) = 0 RETURN n",
      "expect_columns": ["n"],
      "expect_rows": [
        ["({id: 0})"]
      ],
      "ordered": false
    },
    {
      "id": "Comparison1-[15]",
      "description": "null = null",
      "setup": [],
      "query": "RETURN null = null AS value",
      "expect_columns": ["value"],
      "expect_rows": [["null"]],
      "ordered": false
    },
    {
      "id": "Comparison1-[17]",
      "description": "Failing when comparing to an undefined variable",
      "is_error": true,
      "error_type": "SyntaxError",
      "error_contains": "UndefinedVariable",
      "setup": [],
      "query": "MATCH (s) WHERE s.name = undefinedVariable AND s.age = 10 RETURN s"
    }
  ]
}
```

### 3.3 跑步器设计

```cpp
// tests/test_correctness.cpp
class CorrectnessTest : public ::testing::TestWithParam<CorrectnessCase> {
protected:
    void SetUp() override {
        env_ = TestEnv::createFresh();  // 每个用例独立 WiredTiger 实例
    }
    void TearDown() override {
        env_.reset();
    }
    std::unique_ptr<TestEnv> env_;
};

TEST_P(CorrectnessTest, Run) {
    const auto& tc = GetParam();
    // 1. 执行 setup 语句（CREATE 等）
    for (const auto& stmt : tc.setup) env_->execute(stmt);
    // 2. 执行被测查询
    auto result = env_->execute(tc.query);
    // 3. 对比列名和行数据
    EXPECT_COLUMNS(result, tc.expect_columns);
    EXPECT_ROWS_UNORDERED(result, tc.expect_rows);
}

// 从 JSON 文件注册测试
INSTANTIATE_TEST_SUITE_P(
    Comparison1, CorrectnessTest,
    ::testing::ValuesIn(LoadCases("tests/correctness/comparison/Comparison1.json")));
```

### 3.4 关键设计决策

| 决策点 | 选择 | 理由 |
|--------|------|------|
| 中间格式 | JSON（非直接运行 .feature） | 版本可控、人工审查、可增量修改、不依赖 Gherkin 解析器在 CI 中 |
| JSON 存放 | `tests/correctness/` 按 TCK 目录结构组织 | 易于追踪上游源，易于按模块选择性运行 |
| 注册方式 | 文件级 `INSTANTIATE_TEST_SUITE_P` + LoadCases | 每个 .feature → 一组 test suite，ctest 可按文件过滤，失败定位精确 |
| 结果比较 | 自定义 matcher，支持 unordered + null 语义 | TCK 默认无序；`null` 值需要特殊比较逻辑 |
| 错误场景 | 独立 TEST_P 实例，expect query 抛出特定错误 | 与正确场景分离，避免测试污染 |
| setup 执行 | 通过项目的 QueryExecutor 执行 CREATE 语句 | 复用现有 DDL/DML 路径，保证一致性 |

## 四、实施计划

### Phase 1：基础设施（预计 2-3 天）

**1.1 提取脚本 `scripts/extract_tck.py`**
- 使用 Python `gherkin-official` 库解析 `.feature` 文件
- 配置筛选清单（支持的功能列表），跳过不支持的功能
- 输出 JSON 到 `tests/correctness/`，保留源文件路径和版本信息
- 处理 Scenario Outline 参数展开
- 对不支持但相关的场景生成注释掉的占位 JSON

**1.2 跑步器 `tests/test_correctness.cpp`**
- 实现 `CorrectnessCase` 结构体和 JSON 解析（使用 nlohmann/json 或手写简单解析器）
- 实现 `TestEnv`：轻量级环境封装（WiredTiger + QueryExecutor），每个用例独立
- 实现结果比较器：列顺序忽略、行无序匹配、null 值识别、节点/边字面量解析
- 集成到 CMake：`add_executable(correctness_tests ...)` + `gtest_add_tests()`

**1.3 CI 集成**
- 在 `ci.yml` 中添加 `correctness_tests` 目标运行
- 确保 ASan/UBSan 也跑正确性测试

### Phase 2：表达式类用例导入（预计 3-5 天）

按优先级从高到低：

| 批次 | TCK 文件范围 | 预计用例数 | 依赖功能 |
|------|------------|-----------|---------|
| 2a | 比较 1-7 (Comparison*.feature) | ~80 | `=`/`<>`/`<`/`>`/`<=`/`>=` |
| 2b | 布尔 1-5 (Boolean*.feature) | ~60 | AND/OR/NOT |
| 2c | NULL 1-3 (Null*.feature) | ~40 | IS NULL, 三值逻辑传播 |
| 2d | 数学 1-4 (Mathematical*.feature) | ~40 | `+`/`-`/`*`/`/` |

每批流程：
1. 运行提取脚本，自动生成 JSON
2. 人工审查 JSON：标记已知失败（需要后续修复的 bug）、补充注释
3. 运行正确性测试，修复暴露的代码 bug
4. 更新 `docs/query/syntax/cypher-syntax.md` 记录差异

### Phase 3：查询子句类用例导入（预计 3-5 天）

| 批次 | TCK 文件范围 | 预计用例数 | 依赖功能 |
|------|------------|-----------|---------|
| 3a | Match1-5 (基础匹配) | ~60 | MATCH 节点/边/标签 |
| 3b | MatchWhere1-4 | ~50 | WHERE 过滤组合 |
| 3c | Return1-8 | ~80 | RETURN 表达式/别名/DISTINCT |
| 3d | OrderBy/SkipLimit 6+5 | ~80 | 排序 + 分页 |
| 3e | 聚合 1-8 (Aggregation*.feature) | ~80 | count/avg/sum/min/max |

### Phase 4：扩展与持续同步（长期）

- 导入 WITH/CREATE/DELETE/SET 场景（当对应功能实现后）
- 每季度从上游 TCK 拉取新版本，diff 更新
- 将 TCK 用例覆盖统计加入 CI 报告（如：`correctness: 312/350 passed, 38 skipped`）
- 对未实现功能，生成预期失败列表，新人可从中选取任务

## 五、风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| 项目缺少 TCK 依赖的内置函数（`toInteger` 等） | Phase 1 提取脚本中标记并跳过；后续实现函数后补导入 |
| TCK 的数据模型与项目不一致（属性类型 system） | 在 JSON 中标记差异，人工调整期望值；长期对齐类型系统 |
| JSON 文件随 TCK 更新膨胀 | 只提取项目支持范围内的用例；未覆盖的保留注释占位 |
| 结果比较器复杂度（嵌套结构、路径、map） | Phase 1 先支持标量列；后续迭代加入复合类型匹配 |
| 每个用例重建 WiredTiger 开销大 | Phase 1 先简单重建（类似现有集成测试）；后续可优化为快照回滚 |

## 六、当前状态与启用路线图

### 6.1 测试覆盖

| 指标 | 数值 |
|------|------|
| 总场景数 | 1153 |
| 可运行（skip_reason 为空） | ~19 |
| 已跳过（带 skip_reason） | ~1134 |

### 6.2 跳过分组与启用优先级

按影响范围从大到小排列，每一批启用后 `annotate_skip.py` 自动重新分类：

| 优先级 | 类别 | 跳过数 | 需做的工作 | 预计解锁用例 |
|--------|------|--------|-----------|:----------:|
| **P0** | catalog | 204 | 测试框架 SetUp 中解析 setup/query 的标签和属性，调用 `createLabel`/`createEdgeLabel` 预注册 | ~400+ |
| **P1** | engine | 453 | Binder 支持无 MATCH 上下文的独立 RETURN、WITH 管道、多 MATCH | ~400+ |
| **P1** | checker | 261 | Binder 完善语义检查（变量类型冲突、重复绑定、未定义变量） | ~261 (错误场景) |
| **P2** | parser | 138 | ANTLR grammar 添加 UNWIND、IN、XOR 等关键字 | ~138 |
| **P2** | binder | 56 | Binder 实现 DELETE/MERGE/SET/REMOVE 等 DML 子句 | ~56 |
| **P2** | runtime | 22 | 实现 toInteger、toFloat、coalesce 等内置函数 | ~22 |

### 6.3 关键约束

项目是**强类型系统**：
- 标签必须先 `createLabel` 注册，才能 `CREATE` 或 `MATCH`
- 属性列需预先定义，否则 `CREATE` 时属性被静默丢弃
- TCK 的 schema-less 模型（`CREATE (:A {name: 'bar'})` 无需预先定义 A 或 name）与项目类型系统冲突

P0（catalog）解决后，大部分 MATCH-based 查询和带标签的 CREATE 用例即可运行。

## 七、成功标准

1. **Phase 2 完成**：表达式类正确性用例 ≥150 条，全部通过
2. **Phase 3 完成**：查询子句类正确性用例 ≥200 条，全部通过
3. **CI 集成**：correctness_tests 在 PR CI 中自动运行，失败阻断合并
4. **持续同步**：每季度从上游 TCK 更新一次，有清晰的 diff 流程

---

> 参考：
> - [openCypher TCK](https://github.com/opencypher/openCypher) (Apache 2.0)
> - [Gherkin 语法](https://cucumber.io/docs/gherkin/)
> - [gherkin-official (PyPI)](https://pypi.org/project/gherkin-official/)
