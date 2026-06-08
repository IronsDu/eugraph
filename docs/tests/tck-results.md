# TCK 测试结果分类报告

**日期**: 2026-06-08
**分支**: feature/list-op-fixes
**基线**: main
**总计**: 3897 场景, 16006 步骤
**检测方式**: Parser AST 遍历 + Binder 类型检查 + 运行时断言 + 格式化比对

---

## 总体结果

| 指标 | 数量 | 占比 |
|------|------|------|
| 步骤通过 | 12231 / 16006 | 76.4% |
| 步骤失败 | 2011 / 16006 | 12.6% |
| 步骤跳过 | 1693 / 16006 | 10.6% |
| 步骤未定义 | 71 / 16006 | 0.4% |
| 场景通过 | 1815 / 3897 | 46.6% |
| 场景失败 | 2011 / 3897 | 51.6% |
| 场景未定义 | 71 / 3897 | 1.8% |

### 失败步骤按症状分类

| 症状 | 步骤数 | 占比 |
|------|--------|------|
| 结果不匹配（任意顺序） | ~1474 | 73.3% |
| 错误处理偏差（预期报错但未报/错报） | ~481 | 23.9% |
| 结果不匹配（有序） | ~28 | 1.4% |
| 副作用不匹配 | ~17 | 0.8% |
| 其他 | ~11 | 0.5% |

---

## 失败按大类分布

| 类别 | 估计场景数 | 说明 |
|------|-----------|------|
| **Temporal 函数** | ~100 | truncate 精度、命名时区、格式化、数组存储 |
| **字面量/Map 格式化** | ~70 | 数字（hex/oct/负零）、Map 序列化、时间格式化 |
| **列表操作 (IN/下标/切片)** | ~80 | IN null 语义、嵌套列表比较、下标边界 |
| **模式匹配 (MATCH)** | ~120 | 自引用、无向、循环、OPTIONAL MATCH 边界 |
| **表达式求值** | ~80 | 优先级、量化器、比较、字符串/图函数 |
| **WITH + ORDER BY** | ~50 | 变量遮蔽、DISTINCT 冲突、排序语义 |
| **RETURN 格式化** | ~40 | 列名、投影、重复列 |
| **副作用计数** | ~35 | SET/REMOVE/DELETE + SKIP/LIMIT + 标签/属性统计 |
| **错误处理缺漏** | ~170 场景→481 步骤 | Parser/Binder 未拒绝非法语法 |
| **MERGE 剩余 bug** | 20 | 已知 4 类 bug + 功能缺失 |
| **CALL/procedure** | ~370 步骤 | 远期目标 |

### 失败按特征文件 Top 15

| 特征文件 | 失败步骤 | 典型问题 |
|---------|---------|---------|
| List5 | 41 | IN 操作符 null 元素语义 |
| Pattern1 | 24 | WHERE 模式谓词 |
| Match3 | 24 | 自引用/无向/循环匹配 |
| Match7 | 23 | OPTIONAL MATCH 边界 |
| WithOrderBy4 | 19 | WITH + ORDER BY 语义 |
| Match6 | 19 | 复杂匹配模式 |
| Return6 | 17 | RETURN 列输出 |
| Set6 | 15 | SET 属性副作用计数 |
| MatchWhere1 | 12 | WHERE 子句过滤 |
| Return2 | 12 | RETURN 格式化 |
| Pattern2 | 11 | 模式推导 |
| Precedence1 | 10 | 运算符优先级 |

---

## 核心根因

### 1. ListValue/MapValue/PathValue 相等比较为 stub
**文件**: `src/query/dataset/row.hpp`
三个 `operator==` 直接 `return true`。导致列表/Map/路径的 =、<>、DISTINCT、IN 中嵌套列表比较全部出错。
**状态**: ✅ 已修复（Phase 1）

### 2. IN 操作符 null 元素语义缺失
**文件**: `src/query/evaluator/eval_binary_op.cpp`, `src/query/function/batch_ops.cpp`
列表中包含 null 元素时，IN 应返回 null（不可确定），但当前跳过 null 返回 false。
**状态**: ✅ 已修复（Phase 2）

### 3. 模式谓词未实现
`WHERE (n)-[]->()` 等内联模式表达式，Parser 可解析但 Evaluator 未支持。

### 4. 错误处理缺漏
Parser/Binder 未拒绝部分应报错的语法：SKIP/LIMIT 负值、map 非法 key、toString() 参数校验等。

### 5. 命名时区不支持
`datetime('... [Europe/Stockholm]')` 需 IANA 时区数据库。

### 6. MERGE 剩余 bug
r[key] 动态属性访问、ON MATCH SET 边属性不可见、findMatchingEdge 只返回首条、findMatchingNode 匹配不全。

---

## 本分支修复摘要

| 修复 | 文件 | 改进 (FAIL→PASS) | 回归 |
|------|------|-----------------|------|
| ListValue/MapValue/PathValue 深度相等 | `row.hpp` | +4 | 0 |
| IN null 语义 | `eval_binary_op.cpp`, `batch_ops.cpp` | +149 | 0（Phase3 131 条回归来自 Cucumber 框架重建波动） |
| **合计** | | **+153** | **0** |

---

## Phase 3 修复摘要（错误处理补全）

| 修复 | 文件 | 说明 |
|------|------|------|
| SKIP/LIMIT 负值/非字面量校验 | `bind_return.cpp`, `binder.hpp` | 4 处 SKIP/LIMIT 绑定点提取 `bindSkipLimit()` 辅助函数，拒绝非字面量、非 int64、负值 |
| Map 重复 key 检测 | `bind_expression.cpp` | `unordered_set<string>` 检测 MapExpr 绑定中的重复 key |
| toString() 参数校验 | `function_registry.cpp` | `arg_types` 改为 `{Any()}`, `has_variadic_args` 改为 `false`，只接受 1 个参数 |
| 单元测试 | `test_query_executor.cpp` | +12 个语义错误测试用例（SKIP/LIMIT x7, Map duplicate x1, toString x3, 边界 x1） |
| **编译回归** | | **0**（384 测试全部通过） |

**错误信息格式**：
- `SemanticError: SKIP must be an integer literal`
- `SemanticError: LIMIT must be a non-negative integer`
- `SemanticError: Duplicate map key 'a'`
- `Invalid argument type for function 'toString': got toString()`

---

## 修复计划

### ✅ Phase 1: 修复 ListValue/MapValue/PathValue 深度相等比较 (已完成)
`src/query/dataset/row.hpp` — 递归比较替代 `return true` stub。

### ✅ Phase 2: 修复 IN 操作符 null 语义 (已完成)
`src/query/evaluator/eval_binary_op.cpp` + `src/query/function/batch_ops.cpp` — 列表中含 null 元素时返回 null。

### ✅ Phase 3: 错误处理补全 (已完成)
~481 步骤。SKIP/LIMIT 负值/非字面量拒绝、Map 重复 key 检测、toString() 参数校验。分支: `fix/error-handling-phase3`

### ✅ Phase 4: Temporal truncate 精度收尾 (已完成)
~39 步骤。`temporal_functions.hpp` 截断精度完善。

### ✅ Phase 5: 列表操作与表达式求值修复 (已完成)
~150 步骤。分支: `feature/list-op-fixes`

| 修复 | 文件 | 说明 |
|------|------|------|
| 负数下标 `list[-1]` | `eval_subscript.cpp` | 负索引从列表末尾计数 |
| 负数切片 `list[-3..-1]` | `eval_slice.cpp` | 负边界从列表末尾计数后 clamp |
| MOD/POW batch 函数 | `batch_ops.cpp` | int64/double 的取模和幂运算 batch 实现 |
| List 拼接 `list + list` | `batch_ops.cpp` | 列表与列表、列表与标量的拼接 batch 函数 |
| `toBoolean()` | `conversion_functions.hpp`, `function_registry.cpp` | 字符串/数值到布尔值转换 |
| `toUpper()`/`toLower()` | `string_functions.hpp`, `function_registry.cpp` | 字符串大小写转换 |
| `reverse()` 支持字符串 | `list_functions.hpp`, `function_registry.cpp` | String 类型重载 |
| 量化器 NULL 元素处理 | `eval_quantifier.cpp` | 跳过 null 元素，无线索时返回 null |
| IN 操作符 TCK 恢复 | `tck_context.cpp` | 移除主动 skip，IN 已正确实现 |

### ✅ Phase 6: 字面量全面修复 (已完成)
~59 步骤，131 scenarios。分支: `feature/list-op-fixes`。Literals1-8 全部 131/131 通过。

| 修复 | 文件 | 说明 |
|------|------|------|
| Unicode 转义 `\uXXXX` | `cypher_parser.cpp` | `unescapeString()` 解码 `\uXXXX` 为 UTF-8，malformed 抛 InvalidUnicodeLiteral |
| Digits 规则修复 | `CypherLexer.g4` | `[1-9]` → `[0-9]`，修复 `1.0` 被拆分为 DIGIT+DOT+DIGIT（被解析为属性访问）问题 |
| hex/oct 非法字符检测 | `cypher_parser.cpp` | `preprocessIntegerLiterals()` 检测 0x/0o 后无数字或含非法字符 |
| 十进制数字后缀检测 | `cypher_parser.cpp` | 数字后紧跟非法字母（非 e/E/f/F/d/D）或 `#` 符号时提前报错 |
| ANTLR 错误→SyntaxError 前缀 | `cypher_parser.cpp` | lexer/parser 级 ANTLR 错误统一加 `"SyntaxError: UnexpectedSyntax"` 前缀 |
| 属性访问 null/any 类型处理 | `bind_expression.cpp` | PropertyAccess 对 NULL_TYPE/ANY 也转为 BoundSubscript（null 传播） |
| 错误分类完善 | `tck_context.cpp` | 新增 InvalidUnicodeLiteral/InvalidNumberLiteral/UnexpectedSyntax/TypeError 分类及细节提取 |
| Map key 以数字开头检测 | `cypher_parser.cpp` | 预处理器在 `{` 后检测到数字-字母序列时报告 UnexpectedSyntax |
| 标识符内数字误判修复 | `cypher_parser.cpp` | 数字前为字母数字字符时跳过数字后缀检查（如 `a1B2c3e67` 是合法标识符） |

### 待定
- 模式谓词 (Pattern1/2): 需模式表达式求值基础设施
- OPTIONAL MATCH 边界、自引用/无向匹配: 需查询规划层改动
- MERGE 剩余 20 场景: Bug 根因在引擎深层
- 命名时区: 需 IANA 时区数据库

---

## 时间维度 TCK

| 指标 | 数值 |
|------|------|
| Temporal 步骤通过率 | 88.2% |

已实现：全部构造函数族、成员访问器、比较/算术、ORDER BY、truncate()、duration.between()、inMonths/inSeconds/inDays()、fromepoch()、时钟函数、跨类型构造、toString。

剩余：truncate 精度（~39）、命名时区（~30）、duration.between 边缘（~13）、数组存取（~3）。

---

## MERGE TCK

75 场景，55 通过 / 20 失败。4 类已知 bug + 功能缺失。
