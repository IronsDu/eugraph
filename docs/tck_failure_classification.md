# TCK 测试失败分类报告

**日期**: 2026-05-17 (更新)  
**分支**: feature/unlabeled-node-properties  
**总计**: 3897 场景, 16006 步骤  
**运行耗时**: 12 分 40 秒  
**检测方式**: Parser AST 遍历 (替换 regex-based skip)

---

## 总体结果

| 状态 | 场景数 | 占比 |
|------|--------|------|
| 通过 | 44 | 1.1% |
| AST 跳过 (不支持的语法) | 1451 | 37.2% |
| 查询错误 (server 返回错误) | 1438 | 36.9% |
| 结果不匹配 (断言失败) | 802 | 20.6% |
| 未定义步骤 (缺少 step 定义) | 162 | 4.2% |

---

## 分类 1: AST 精确 SKIP — 不支持的语法（设计如此，不修复）

1451 个场景因查询使用了尚未实现的语法而被 AST 遍历器跳过。跳过是预期的，因为对应的查询引擎能力尚未实现。

### 1.1 不支持的子句类型

| 子句 | 跳过次数 | 优先级 | 说明 |
|------|---------|--------|------|
| UNWIND | 191 | P1 | 展开列表为行 |
| MERGE | 72 | P1 | 合并创建，需要条件扫描+创建逻辑 |
| DELETE / DETACH DELETE | 41 | P1 | 删除顶点/边 |
| OPTIONAL MATCH | 78 | P1 | 可选匹配，需要 outer join 语义 |
| REMOVE | 30 | P2 | 删除属性/标签 |
| CALL | 2 | P3 | 存储过程调用 |
| standalone CALL | 1 | P3 | 顶层 CALL |
| UNION | 12 | P2 | 查询结果合并 |

### 1.2 不支持的表达式

| 表达式 | 跳过次数 | 优先级 | 说明 |
|--------|---------|--------|------|
| 量词 (ALL/ANY/NONE/SINGLE) | 532 | P2 | 列表量词谓词 |
| IN 运算符 | 62 | P2 | `x IN [1,2,3]` |
| XOR 运算符 | 30 | P3 | 异或布尔运算 |
| CASE 表达式 | 12 | P2 | 条件表达式 |
| STARTS WITH | 11 | P2 | 字符串前缀匹配 |
| ENDS WITH | 8 | P2 | 字符串后缀匹配 |
| CONTAINS | 8 | P2 | 字符串包含 |
| EXISTS 子查询 | 10 | P2 | 存在性子查询 |
| 模式推导 (Pattern comprehension) | 15 | P3 | `[x IN ... \| ...]` |

### 1.3 不支持的语法模式

| 模式 | 跳过次数 | 优先级 | 说明 |
|------|---------|--------|------|
| Parser 限制 (无法解析) | 232 | P3 | 各种 parser 无法处理的语法变体 |
| 无上界变长扩展 | 77 | P2 | `[:T*]` 无 max 上界，DFS 资源耗尽 |
| 多标签节点 | 25 | P2 | `CREATE (:A:B)` |
| 关系类型交替 | 3 | P2 | `[:A|B]` |

**结论**: 此分类均属于查询引擎功能缺失，不需要修改 TCK 框架。优先在后续版本中实现对应功能。

---

## 分类 2: 缺少步骤定义（Undefined Steps）

162 个场景因缺少 Gherkin 步骤定义而标记为 undefined。

### 2.1 步骤模式统计

| 缺失步骤模式 | 涉及场景数 | 优先级 | 说明 |
|-------------|-----------|--------|------|
| `And parameters are:` | 64 | P2 | 参数化查询 ($param) |
| `And there exists a procedure ...` | 50 | P3 | 存储过程定义 (CALL 子句前置) |
| `When executing control query:` | 33 | P2 | 控制查询（setup 验证） |
| `Then the result should be (ignoring element order for lists)` | 23 | P2 | 列表忽略顺序比较 |
| `Then a TypeError should be raised at any time: ...` | 18 | P2 | 任意阶段抛 TypeError |
| `Then a ProcedureError should be raised ...` | 2 | P3 | 存储过程错误 |
| `Given the binary-tree-N graph` | 19 | P3 | 预定义图结构（二叉树） |

### 2.2 分析与建议

- **参数支持** (`parameters are:`) 和 **控制查询** (`executing control query:`) 是 TCK 框架的基础能力，建议 P2 实现
- **列表忽略顺序比较** 可用于当前 MATCH 结果验证
- **存储过程** 相关步骤是远期目标
- **预定义图结构** 需要根据 TCK feature 文件定义来加载

---

## 分类 3: 查询错误 — 服务端返回错误

1438 个场景的服务端查询返回了错误。按根因分类如下：

### 3.1 绑定错误 — 无返回列 (NothingToReturn)

**次数**: 1077  
**根因**: Binder 遇到无法绑定的模式后，没有可用的 RETURN 列。通常是被其他绑定错误级联触发（如分类 3.2~3.5 的前置错误）。

### 3.2 绑定错误 — 变量未定义 (UndefinedVariable)

**次数**: 199  
**根因**: 查询引用了不在作用域内的变量名，或 TCK 测试用例预期抛出 UndefinedVariable 错误。

### 3.3 绑定错误 — 类型不匹配 (TypeMismatch)

**次数**: 67  
**根因**: 算术运算/比较操作接收了非数值类型（如 `ANY + INT64`, `STRING + STRING` 等）。

### 3.4 绑定错误 — 函数未找到 (UnknownFunction)

**次数**: 37  
**根因**: 使用了尚未注册的函数（如 `labels(VERTEX)` 等）。

### 3.5 绑定错误 — 属性访问非实体类型 (Property on non-entity)

**次数**: 22  
**根因**: 对非顶点/边类型（BOOL、STRING、LIST 等）进行属性访问（`n.name` 其中 n 是标量）。

### 3.6 绑定错误 — 不支持的 SET 语法

**次数**: 8  
**根因**: `SET n += {props}` 等动态属性设置语法尚未实现。

### 3.7 其他绑定错误

| 错误 | 次数 | 说明 |
|------|------|------|
| 标签不存在 (UnknownLabel) | 16 | 引用未创建的标签 |
| 边类型不存在 (UnknownEdgeType) | 2 | 引用未创建的边类型 |
| 混合路径不支持 (Mixed path) | 3 | 定长/变长混合路径链 |
| 无效变长范围 (Invalid VLE range) | 3 | 变长跳数范围无效 |
| 无效参数类型 | 4 | 函数参数类型不匹配 |

---

## 分类 4: 结果不匹配 — 断言失败

802 个场景的查询成功执行，但返回结果与 TCK 预期不符。

### 4.1 可能原因

- **排序差异**: TCK 期望有序结果但 server 返回无序（或反之）
- **格式差异**: 数值精度（浮点数）、NULL 表示、列表格式等
- **路径格式**: 路径的序列化/反序列化格式差异
- **未实现语义**: 某些 Cypher 语义（如聚合空值处理、null 传播等）行为与标准不一致

### 4.2 优先级: P2

需要逐一分析失败场景，区分是格式问题还是语义问题。

---

## 已修复分类汇总

| 分类 | 修复前 | 修复后 | 状态 |
|------|--------|--------|------|
| 分类: 无标签节点属性 (`Property not found`) | 444 | 0 | ✅ |
| 分类: 边类型自动创建 (`Edge type does not exist`) | ~8 | 0 | ✅ |
| 分类: type/last 函数 (`Function not found`) | ~6 | 0 | ✅ |
| 分类: WITH/MATCH 顺序 | ~24 | 0 | ✅ |
| 分类: WITH 为首子句 | 478 | 0 | ✅ |
| 分类: 逗号分隔 CREATE 路径 | 187 | 0 | ✅ |
| 分类: 多个 MATCH 子句 | 198 | 0 | ✅ |
| 分类: 优化器无限循环 (FilterPushdown) | 导致超时卡死 | 已修复 | ✅ |

---

## 优先级汇总

| 优先级 | 分类 | 影响场景数 | 修复路径 |
|--------|------|-----------|---------|
| **P1** | 分类1: MERGE/DELETE/UNWIND/OPTIONAL | ~421 | 逐个实现子句 |
| **P1** | 分类1: 量词表达式 (ALL/ANY/NONE/SINGLE) | 532 | 实现列表量词 |
| **P2** | 分类2: 缺失步骤定义 | 162 | 补实现步骤 |
| **P2** | 分类4: 结果不匹配 | 802 | 逐一分析 |
| **P2** | 分类3: NothingToReturn 级联错误 | 1077 | 修复前置绑定问题 |
| **P2** | 分类1: CASE/IN/STARTS WITH/ENDS WITH 等 | ~123 | 实现表达式 |
| **P2** | 分类7: 错误消息不匹配 | ~50 | 错误分类精细化 |
| **P3** | 分类1: Parser 限制 | 232 | Parser 增强 |
| **P3** | 分类3: 其他绑定错误 | ~30 | 后续迭代 |

---

## 测试基础设施改进

本次更新包含以下修复：

1. **优化器无限循环修复**: `FilterPushdownRule` 允许 Filter 穿透 Filter 导致对换循环（`Filter_A → Filter_B` 变成 `Filter_B → Filter_A` 再变回来，无限循环）。`findDuplicate` 只检查 group_id + child_groups + variant index，不比较算子内容，因此无法识别语义等价。修复：从 `isPenetrable` 中移除 `OptNodeType::Filter`，并增加 1024 次最大迭代安全限制。
