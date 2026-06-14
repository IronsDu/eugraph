# TCK 测试结果分类报告

**日期**: 2026-06-14
**分支**: feature/list-tck-fixes
**基线**: main

---

## 总体结果

| 指标 | 数量 |
|------|------|
| 场景通过 | **3316 / 3897** |
| 场景失败 | 510 |
| 场景未定义 | 71 |
| Skip 步骤 | 575 |

---

## 表达式类

| 类别 | 场景数 | 通过 | 失败 | 主要问题 |
|------|--------|------|------|---------|
| Literals | 131 | **131** | 0 | ✅ 已全部修复 |
| Conditional | 13 | **13** | 0 | ✅ 全部通过 |
| Boolean | 150 | **150** | 0 | ✅ 已全部修复 |
| Map | 44 | **44** | 0 | ✅ 已全部修复 |
| Null | 44 | **44** | 0 | ✅ 已全部修复 |
| Path | 7 | 5 | 2 | 路径表达式 |
| Mathematical | 6 | **6** | 0 | ✅ 已全部修复 |
| String | 32 | **32** | 0 | ✅ 已全部修复 |
| Precedence | 121 | **121** | 0 | ✅ 已全部修复 |
| TypeConversion | 47 | **44** | 3 | toString/list/comprehension 已实现，3个跨积(MATCH...WITH * MATCH)预存问题 |
| Comparison | 72 | 62 | 10 | 预存问题：`toInteger`+下标+MATCH、命名路径、labels加载、复杂属性跨类型 |
| Aggregation | 35 | 16 | 19 | 聚合函数、分组语义 |
| List | 185 | **181** | 4 | Pattern comprehension 未实现（List6[7-10]），详见下方 |
| Graph | 61 | 26 | 35 | nodes()/relationships()/labels() 等图函数 |
| Quantifier | 604 | **604** | 0 | ✅ 已全部修复（嵌套量词 ANY 类型、路径元素序列化） |
| Temporal | 1004 | 645 | 359 | **improving**: truncate 精度、命名时区格式化、duration toString/计算、解析单复数/时区处理 |
| ExistentialSubqueries | 10 | 2 | 8 | EXISTS 子查询 |

## 子句类

| 类别 | 场景数 | 通过 | 失败 | 主要问题 |
|------|--------|------|------|---------|
| MERGE | 75 | 55 | 20 | ON MATCH/CREATE 副作用、动态属性 |
| Remove | 33 | 18 | 15 | 属性/标签移除副作用计数 |
| Delete | 41 | 17 | 24 | DETACH DELETE 副作用计数 |
| Create | 78 | 43 | 35 | 创建模式、属性副作用 |
| RETURN | 63 | 23 | 40 | 列名生成、投影、DISTINCT、ORDER BY |
| Set | 53 | 8 | 45 | SET 属性/标签副作用计数 |
| WITH | 29 | 9 | 20 | 变量遮蔽、DISTINCT、ORDER BY、WHERE |
| MatchWhere | 34 | 5 | 29 | WHERE 模式谓词、逻辑组合 |
| CALL | 52 | 0 | 0 | 52 undefined（远期目标） |

## useCases

| 类别 | 场景数 | 通过 | 失败 | 未定义 |
|------|--------|------|------|--------|
| useCases | 30 | 4 | 7 | 19 |

---

## 已修复记录

| Phase | 分支 | 内容 |
|-------|------|------|
| Phase 1 | fix/value-deep-equality | ListValue/MapValue/PathValue 深度相等比较 |
| Phase 2 | feature/list-op-fixes | IN 操作符 null 语义 |
| Phase 3 | fix/error-handling-phase3 | SKIP/LIMIT 负值拒绝、Map 重复 key 检测、toString() 参数校验 |
| Phase 4 | fix/temporal-truncate | Temporal truncate 精度收尾 |
| Phase 5 | feature/list-op-fixes | 负数下标/切片、MOD/POW、list 拼接、toBoolean/toUpper/toLower、reverse 字符串、量化器 NULL 处理 |
| Phase 6 | fix/literal-map-formatting | Literals1-8 全部 131/131：Unicode 转义、Digits 规则、hex/oct 检测、ANTLR 错误前缀、属性访问 null 传播、Map key 检测 |
| Phase 7 | feature/update-tck-report | Mathematical 6/6：abs/sqrt 函数实现、ANTLR 运算符 token 索引修复、expressionToString 括号/去尾零、U+2014 检测、ANY 类型算术退路 |
| Phase 8 | feature/boolean-map-null-fixes | Map 44/44+Null 44/44：递归参数解析(嵌套 Map/List/null)、运行时 subscript 类型错误、LeftJoin projection pushdown、IN + null binder 放宽/求值修复、OPTIONAL MATCH 首子句支持+属性加载、vertex JSON 格式空格修复 |
| Phase 9 | feature/comparison-null-semantics | Comparison 29→62 (+33)、String 8→25 (+17)：三值相等 valueEquals（List/Map/Path 深度 null 传播+int64/double 提升）、compareValues 三值有序比较、链式比较展开（`a<b<c` → `(a<b) AND (b<c)`）、double 除零返回 NaN、numericPair sentinel 改为 optional、List 有序比较、STARTS_WITH/ENDS_WITH/CONTAINS 跨类型允许+NULL_TYPE dispatch |
| Phase 10 | feature/comparison-null-semantics | Precedence 92→93 (+1)：新增 ParenExpr AST 节点保留括号信息防止 hoistAtomicPredicates 错误提升、genericAddBatch 增加 List 运行时处理修复 ANY+INT64 的 ADD 误分发到 int64AddBatch |
| Phase 11 | feature/fix-boolean-remaining | String 25→32 (+7)：unescapeString 标准转义序列处理（\n,\t,\\等）、语法重构对齐 openCypher BNF `<linear statement>`、expressionToString 字符串字面量加引号、Precedence 93→121 |
| Phase 12 | feature/temporal-tck-fixes | **场景级**: 33/88→47/88 (+14 场景)。**Step 级**: 600→645 passed (+45), 404→359 failed (-45)。修复：duration toString 移除 weeks、extractDateFields 用 ISO week-year、parseTzOffset 命名时区不崩溃、parseDatetimeStr/parseTimeStr [ZoneName] 支持、isoWeekNumber 边界检测、duration.inDays/Months/Seconds 返回 DurationValue、base date dayOfWeek 被显式 year 覆盖、extractNanosFromMap 单复数、durationBetween 符号/时区/months_diff==0、'time' key 支持 |

### List TCK 修复详情 (Phase 13)

**分支**: `feature/list-tck-fixes`
**日期**: 2026-06-14
**结果**: 91 → **181** / 185 (+90 scenarios, +445 steps)

#### 引擎变更

| 文件 | 变更 |
|------|------|
| `src/query/optimizer/rules/filter_pushdown.cpp` | `collectColumnNames()` / `producedVariableNames()` 实现变量重叠检查，替换原有的保守 `VarLenExpand` 跳过 |
| `src/query/evaluator/eval_slice.cpp` | 重构：null 边界返回 null；负索引规范化 |
| `src/query/evaluator/eval_binary_op.cpp` | `IN` 三值逻辑（`valueEquals` → `optional<bool>`） |
| `src/query/function/batch_ops.cpp` | `inBatch` 三值逻辑同步 |
| `src/query/function/function_registry.cpp` | 新增 `sign()`；`range()` 参数放宽到 ANY，类型检查移至运行时 |
| `src/query/function/scalar/list_functions.hpp` | `size(null)` → null；`range()` 类型/step=0 改为 ArgumentError |
| `src/query/function/scalar/math_functions.hpp` | 新增 `sign()` |
| `src/query/parser/cypher_parser.cpp` | 裸 pattern 表达式在 value 上下文抛 UnexpectedSyntax；IN 优先级/下标链修复 |
| `src/query/parser/ast.hpp` | `expressionToString` 补全 ListExpr/MapExpr/CaseExpr/ExistsExpr |
| `src/query/physical_plan/operator/aggregate_physical_op.cpp` | 复杂聚合 0 行输入时使用空初始化值 |
| `src/query/physical_plan/operator/set_physical_op.cpp` | SET 后同步更新内存顶点属性 |
| `src/query/planner/binder/bind_expression.cpp` | 函数 overload 失败 → `SyntaxError: InvalidArgumentType` |
| `src/query/planner/binder/bind_return.cpp` | 两阶段聚合检测，仅存在复杂聚合时才插 ProjectOp |

#### 剩余 4 个失败：Pattern Comprehension 未实现

List6.feature [7]-[10] 全部使用 `[(pattern) | expr]` 语法，该特性在引擎中只解析不执行。本质是**相关子查询**（correlated subquery），需要 Binder → Evaluator → 物理算子三层实现，应作为独立工作项。

#### 单元测试

全部 624 用例通过，无回归。

---

## 推荐实施顺序

### 低风险（局部表达式求值改动）

1. **Boolean (148→150)**：仅剩 2 失败，几乎完美
2. **Map (38→44)**：✅ 已全部修复
3. **Null (31→44)**：✅ 已全部修复
4. **String (8→25)**：+17，剩余 7 失败（newline 转义 3 + 非字符串 UNWIND 3 + reverse 1）
5. **TypeConversion (14→44)**：✅ 基本完成，仅剩 3 个跨积预存问题
6. **Comparison (29→62)**：+33，剩余 10 失败（全部为预存问题）

### 中风险（涉及运算符/比较语义）

8. List (91→185)：94 失败，IN/null 语义、嵌套比较、下标/切片边界
9. ~~Quantifier (479→604)~~ ✅ 已全部修复（604/604）：嵌套量词 ANY 类型 fallback、路径元素属性/标签加载、list_json 序列化

### 高风险（涉及规划/引擎层改动）

10. RETURN 格式化、Aggregation
11. Temporal、Graph
12. MATCH、MERGE、WITH、SET、CREATE、DELETE、Remove

### 远期目标

13. CALL/procedure (52 undefined)

---

## Temporal 修复详情 (Phase 12)

**分支**: `feature/temporal-tck-fixes`
**日期**: 2026-06-11

### Feature 级进度

| Feature | 修复前 | 修复后 | 变化 |
|---------|--------|--------|------|
| Temporal1 (构造) | 8/13 | **12/13** | +4 |
| Temporal2 (解析) | 0/7 | 0/7 | — (string parse 边界) |
| Temporal3 (选择) | 4/11 | **6/11** | +2 |
| Temporal4 (存储) | 6/13 | 6/13 | — (数组+时区) |
| Temporal5 (访问器) | 2/7 | **3/7** | +1 |
| Temporal6 (序列化) | 5/7 | 5/7 | — |
| Temporal7 (比较) | 6/6 | 6/6 | ✅ 完全通过 |
| Temporal8 (算术) | 0/7 | **4/7** | +4 |
| Temporal9 (截断) | 2/5 | **4/5** | +2 |
| Temporal10 (duration.between) | 0/12 | **1/12** | +1 |
| **总计** | **33/88** | **47/88** | **+14** |

### 已修复 Bug 清单

| # | Bug | 文件 |
|---|-----|------|
| 1 | Duration toString 保留 weeks → 统一输出 days | `temporal_value.cpp:714` |
| 2 | extractDateFields 用日历年份 → 用 ISO week-year | `temporal_functions.hpp:226` |
| 3 | parseTzOffset 对命名时区崩溃 → 安全返回 0 | `temporal_functions.hpp:148` |
| 4 | parseDatetimeStr/parseTimeStr 不处理 `[ZoneName]` 后缀 | `temporal_functions.hpp:390,352` |
| 5 | isoWeekNumber 不检测周一前日期 → `day_of_year < monday_week1_day` | `temporal_functions.hpp:72` |
| 6 | duration.inDays/Months/Seconds 返回 double → DurationValue | `temporal_functions.hpp:1802+`, `function_registry.cpp` |
| 7 | base date 的 dayOfWeek 被显式 year 覆盖 → 保存原始 base 字段 | `temporal_functions.hpp:197,216,226,240` |
| 8 | extractNanosFromMap 只支持单数形式 → 同时支持单/复数 | `temporal_functions.hpp:135` |
| 9 | durationBetween(Time) 用 a-b → b-a | `temporal_value.cpp:587` |
| 10 | durationBetween months_diff==0 时不该借月份 | `temporal_value.cpp:549` |
| 11 | durationBetween DATE vs DATETIME 时区不一致 | `temporal_value.cpp:567` |
| 12 | localdatetime/datetime 构造器不处理 'time' key | `temporal_functions.hpp:806,880` |
| 13 | fmtTimezone 命名时区总是 +00:00 → 用实际 offset | `temporal_value.cpp:658` |
| 14 | parseDatetimeStr 不支持 week/ordinal 日期格式 | `temporal_functions.hpp:390` |
| 15 | parseTimeStr 不支持简写（如 '21' 只给小时） | `temporal_functions.hpp:359` |

### 剩余失败分类 (41 场景)

| 类别 | 数量 | 说明 |
|------|------|------|
| IANA 时区偏移 | ~30 step failures (2 场景) | 需要 IANA 时区数据库才能根据 `Europe/Stockholm` + 日期计算正确 UTC 偏移 |
| duration.between 精度 | ~8 场景 | 浮点精度、inDays/inMonths 月份转天数近似值 |
| 解析边界情况 | ~5 场景 | week/ordinal 格式 datetime string、compact time format |
| 算术精度 | ~3 场景 | 浮点 carry chain、时区保留 |
| Store 数组 | ~7 场景 | temporal 数组属性 Thrift 格式化 |
| 其他 | ~18 | truncate 时区叠加、serialize 边界等 |
