# TCK 测试结果分类报告

**日期**: 2026-06-10
**分支**: feature/comparison-null-semantics
**基线**: main

---

## 总体结果

| 指标 | 数量 |
|------|------|
| 场景通过 | ~2425 / 3897 |
| 场景失败 | ~1401 |
| 场景未定义 | ~71 (CALL/procedure + useCases) |

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
| String | 32 | 25 | 7 | newline 转义、非字符串操作数 UNWIND+聚合、reverse 列名格式化 |
| TypeConversion | 47 | **44** | 3 | toString/list/comprehension 已实现，3个跨积(MATCH...WITH * MATCH)预存问题 |
| Precedence | 121 | 92 | 29 | 运算符优先级 |
| Comparison | 72 | 62 | 10 | 预存问题：`toInteger`+下标+MATCH、命名路径、labels加载、复杂属性跨类型 |
| Aggregation | 35 | 16 | 19 | 聚合函数、分组语义 |
| List | 185 | 91 | 94 | IN/null 语义、嵌套比较、下标/切片边界 |
| Graph | 61 | 26 | 35 | nodes()/relationships()/labels() 等图函数 |
| Quantifier | 604 | 479 | 125 | ALL/ANY/NONE/SINGLE 量化器边界 |
| Temporal | 1004 | 600 | 404 | truncate 精度、命名时区、格式化、数组存取 |
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

## 推荐实施顺序

### 低风险（局部表达式求值改动）

1. **Boolean (148→150)**：仅剩 2 失败，几乎完美
2. **Map (38→44)**：✅ 已全部修复
3. **Null (31→44)**：✅ 已全部修复
4. **String (8→25)**：+17，剩余 7 失败（newline 转义 3 + 非字符串 UNWIND 3 + reverse 1）
5. **TypeConversion (14→44)**：✅ 基本完成，仅剩 3 个跨积预存问题
6. **Comparison (29→62)**：+33，剩余 10 失败（全部为预存问题）
7. **Precedence (92→121)**：29 失败，运算符优先级

### 中风险（涉及运算符/比较语义）

8. List (91→185)：94 失败，IN/null 语义、嵌套比较、下标/切片边界
9. Quantifier (479→604)：125 失败，ALL/ANY/NONE/SINGLE 量化器边界

### 高风险（涉及规划/引擎层改动）

10. RETURN 格式化、Aggregation
11. Temporal、Graph
12. MATCH、MERGE、WITH、SET、CREATE、DELETE、Remove

### 远期目标

13. CALL/procedure (52 undefined)
