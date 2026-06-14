# List TCK 修复报告

**日期**: 2026-06-14
**分支**: feature/list-tck-fixes
**范围**: `third_party/openCypher/tck/features/expressions/list/`

---

## 总体结果

### List TCK

| 指标 | 数量 |
|------|------|
| 总场景数 | 185 |
| 通过 | **181** |
| 失败 | 4（全部为未实现的 pattern comprehension） |

### 全量 TCK

| 指标 | 修复前（main 基线） | 修复后 | 变化 |
|------|---------------------|--------|------|
| 通过场景 | 2637 / 3897 | **3316 / 3897** | **+679** |
| 失败场景 | 1189 | 510 | **−679** |
| 未定义场景 | 71 | 71 | — |
| Skip 步骤 | 1147 | 575 | −572 |

### 单元测试

| 测试套件 | 用例数 | 结果 |
|---------|-------|------|
| cypher_parser_tests | 81 | ✅ 全部通过 |
| optimizer_tests | 9 | ✅ 全部通过 |
| eugraph_tests | 74 | ✅ 全部通过 |
| query_executor_tests | 384 | ✅ 全部通过 |
| metadata_tests | 17 | ✅ 全部通过 |
| catalog_tests | 23 | ✅ 全部通过 |
| index_codec_tests | 13 | ✅ 全部通过 |
| index_store_tests | 14 | ✅ 全部通过 |
| vertex_serialization_tests | 4 | ✅ 全部通过 |
| wt_tests | 7 | ✅ 全部通过 |

**无回归**。

---

## 剩余 4 个失败：Pattern Comprehension 未实现

List6.feature 的 [7]、[8]、[9]、[10] 这 4 个场景全部使用 pattern comprehension 语法
`[(pattern) | expr]`，该特性在引擎中**只解析不执行**。

### 失败场景

| 场景 | 查询 |
|------|------|
| List6[7] | `MATCH (n:X) RETURN n, size([(n)--() \| 1]) > 0 AS b` |
| List6[8] | `MATCH (a:X) RETURN size([(a)-->() \| 1]) AS length` |
| List6[9] | `MATCH (a:X) RETURN size([(a)-[:T]->() \| 1]) AS length` |
| List6[10] | `MATCH (a:X) RETURN size([(a)-[:T\|OTHER]->() \| 1]) AS length` |

### 实现状态

| 层 | 状态 |
|----|------|
| ANTLR Grammar | ✅ 已有 `patternComprehension` 规则 |
| Parser AST | ✅ 已有 `ast::PatternComprehension` 节点（`src/query/parser/ast.hpp:180`） |
| Binder | ❌ `bind_expression.cpp` 无对应 case |
| Physical Plan | ❌ 无物理算子 |
| Evaluator | ❌ 无求值逻辑 |
| TCK Skip | ⚠️ `tck_context.cpp:172` 检测到后仅打印日志、`GTEST_SKIP`，但后续 THEN 步骤仍执行并失败 |

### Pattern Comprehension 的本质

这个特性本质上是**相关子查询（correlated subquery）**：

1. 对外层每一行（如 `MATCH (a:X)` 的每个 `a`）
2. 用 `a` 作为已知绑定，执行内层 pattern match（如 `(a)-->()`）
3. 对每个匹配求值投影表达式（如 `1`）
4. 收集成 list 返回

实现它需要：

1. **Binder**：把 `[(pattern) | projection]` 编译成 BoundCorrelatedSubquery，外层变量通过参数传递给内层
2. **物理算子**：新增 Apply/NestedLoop 算子，对外层每个 chunk 触发一次内层模式匹配
3. **变量作用域**：处理外层 → 内层的变量绑定（外层 `a` 在内层为已知节点）

这是一个独立的中等规模特性，超出 list TCK 修复的范围，应作为单独的工作项处理。

### 推荐处理方式

不要把这 4 个失败塞进当前 PR。建议：

1. 创建独立 issue/branch：`feature/pattern-comprehension`
2. 设计文档：`docs/query/engine/pattern-comprehension-design.md`
3. 实现顺序：Binder → Evaluator → 物理算子 → TCK 验证

---

## 本次修复涉及的变更

### 核心引擎变更

| 文件 | 变更摘要 |
|------|---------|
| `src/query/optimizer/rules/filter_pushdown.cpp` | 新增 `collectColumnNames()` / `producedVariableNames()`；Filter 下推前检查谓词引用变量与子算子产出变量的重叠，避免下推到变量尚未定义处 |
| `src/query/evaluator/eval_slice.cpp` | 重构 slice 求值：抽出 `finishSlice()` 处理负索引与裁剪；null 边界返回 null |
| `src/query/evaluator/eval_binary_op.cpp` | `IN` 使用三值逻辑（`valueEquals` 返回 `optional<bool>`），嵌套 null 正确传播 |
| `src/query/function/batch_ops.cpp` | `inBatch` 同步三值逻辑修复 |
| `src/query/function/function_registry.cpp` | 新增 `sign()` 重载；`range()` 参数放宽到 ANY，类型检查移到运行时（符合 Cypher 语义） |
| `src/query/function/scalar/list_functions.hpp` | `size(null)` 返回 null（非 0）；`range()` 参数类型与 step=0 改为运行时 ArgumentError |
| `src/query/function/scalar/math_functions.hpp` | 新增 `sign()` 实现 |
| `src/query/parser/cypher_parser.cpp` | 裸 pattern 表达式作为 value 上下文使用时抛 `UnexpectedSyntax`；IN 优先级与下标链修复 |
| `src/query/parser/ast.hpp` | `expressionToString` 补全 ListExpr / MapExpr / CaseExpr / ExistsExpr 字符串化 |
| `src/query/physical_plan/operator/aggregate_physical_op.cpp` | 复杂聚合（如 `size(collect(...))`）在 0 行输入时使用空初始化值 |
| `src/query/physical_plan/operator/set_physical_op.cpp` | SET 后同步更新内存中顶点的属性，使后续 RETURN/WITH 能见到新值 |
| `src/query/planner/binder/bind_expression.cpp` | 函数 overload 失败改为 `SyntaxError: InvalidArgumentType`（符合 TCK 约定） |
| `src/query/planner/binder/bind_return.cpp` | 两阶段聚合检测：仅当存在复杂聚合时才插入 ProjectOp，避免丢失 group key |

### 测试相关变更

| 文件 | 变更摘要 |
|------|---------|
| `tests/tck/tck_context.cpp` | 副作用计算改用 per-key diff（属性修改计为 +1/−1）；移除 unbounded varlen 跳过 |
| `tests/tck/tck_context.hpp` / `tck_types.hpp` | 新增 `PropertyMap` 跟踪每个 (element_id, prop_name) 的值 |
| `tests/test_query_executor.cpp` | `ToString*Fails` 断言更新为新错误消息格式 |

---

## 文档更新

- `docs/query/optimizer/cascades-optimizer.md`：FilterPushdown 章节补充变量重叠检查逻辑
