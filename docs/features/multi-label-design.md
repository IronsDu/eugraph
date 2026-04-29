# 多标签（Multi-Label）查询设计

> 状态：需求讨论阶段，尚未实现
> 相关模块：Cypher 解析、逻辑计划、物理计划、存储编码、表达式求值

---

## 一、设计动机

当前图数据库在 Schema 设计上存在两个极端：

| 系统 | 问题 |
|------|------|
| **NebulaGraph** | 强 Schema 过度僵硬：属性严格绑定在标签上，跨标签查询实体属性极其繁琐，开发者必须时刻记住属性属于哪个标签 |
| **Neo4j** | 无 Schema/弱 Schema 过度自由：属性是键值对"便利贴"，缺乏静态类型安全保障，核心业务逻辑容易因拼写错误或类型不匹配而静默失败 |

本设计提出一种**双模式查询机制**，在两者之间架设桥梁：

> "日常探索追求便捷（n.name），关键业务逻辑追求类型安全（n.(B).name）"

---

## 二、设计方案

### 模式一：自动合并（便捷模式）

```cypher
MATCH (n:TagA) RETURN n.name
```

- 执行引擎从该顶点**所有标签**的属性集中查找 `name`
- 多个标签都有 `name` 时：合并返回（冲突策略待定，见第四章）
- 开发者不需要知道属性属于哪个标签 — 以实体为中心访问数据

### 模式二：静态转型（类型安全模式）

```cypher
MATCH (n:TagA) RETURN n.(TagB).name
```

- 显式以 TagB 的视角访问属性，限定只在 TagB 的属性中查找
- **编译期**（执行前）：校验 TagB 是否定义了 `name` 属性，没有则报错
- **运行期**：如果顶点 n 没有绑定 TagB，返回 null（类似 LEFT JOIN 语义）

#### 语法扩展：WHERE 子句

```cypher
MATCH (n:Person) WHERE n.(Employee).salary > 10000 RETURN n.name
```

转型语法同样适用于 WHERE 中的属性访问。

---

## 三、当前代码现状分析

### 3.1 各路径多标签支持情况

| 路径 | 现状 | 问题 |
|------|------|------|
| **解析器（Parser）** | `NodePattern.labels` 是 `vector<string>`，语法已支持 `(n:A:B)` | 无 |
| **逻辑计划（LabelScanOp）** | 只使用 `labels[0]` | 后几个标签被静默丢弃 |
| **物理计划（LabelScanPhysicalOp）** | 单 LabelId | 只从单个标签表加载属性 |
| **物理计划（AllNodeScanPhysicalOp）** | 按标签分裂 | 同一顶点多标签时出现多次 |
| **物理计划（ExpandPhysicalOp）** | 加载所有标签并合并 | 有，但 prop_id 碰撞 bug |
| **表达式求值（PropertyAccess）** | 遍历所有 label_defs 搜索属性名 | 同样有 prop_id 碰撞问题 |
| **CREATE 路径** | 完整支持多标签 | 无 |

### 3.2 核心 Bug：prop_id 碰撞

属性按标签存储在不同 `vprop_{id}` 表中，prop_id 在每个 Label 内独立编号（从 1 开始）。当 ExpandPhysicalOp 合并多个标签的属性时，不同标签的 prop_id 可能相同但代表不同属性，直接合并到同一个 `vector<optional<PropertyValue>>`（按 prop_id 下标）中会导致**后者覆盖前者**。

表达式求值也有类似问题：遍历所有 label_defs 查找属性名时，找到后按 prop_id 去 `VertexValue.properties` 取值，但如果该 prop_id 被另一个标签的属性覆盖，就会取到错误的值。

### 3.3 现有数据模型（可复用）

- 顶点已支持 `LabelIdSet`（多标签）
- 属性按标签独立存储在 `vprop_{id}` 表（每标签一张表）
- 标签反向索引 `label_reverse` 表已维护顶点→标签映射
- `GraphSchema` 已维护标签名→ID 双向映射

---

## 四、待决策问题

### 4.1 属性冲突解决策略

当 TagA 有 `name: "张三"`，TagB 有 `name: "张大壮"`，`n.name` 返回什么？

- **方案 A — 严格模式**：抛出歧义错误，强制开发者用 `n.(A).name` 或 `n.(B).name` 消除歧义
- **方案 B — 优先匹配**：按标签优先级或扫描顺序返回第一个匹配值
- **方案 C — 合并为列表**：返回 `["张三", "张大壮"]`，最灵活但增加客户端解析负担

### 4.2 转型语法终稿

- **方案 A**：`n.(B).name` — 类似 C++ 显式转型或 TypeScript 类型断言
- **方案 B**：`n::B.name` — 更接近 C++ 作用域限定符
- **方案 C**：`n[B].name` — 下标式访问

当前倾向：`n.(B).name`

### 4.3 属性存储模型变更

属性当前按 prop_id（uint16，每标签内独立编号）索引。跨标签合并时必然碰撞。方案：

- **方案 A — 名称索引**：`VertexValue.properties` 从 `vector<optional<PropertyValue>>` 改为 `map<string, PropertyValue>`，按属性名直接索引（简化查询，但改动面大，增加内存和序列化开销）
- **方案 B — 全局 prop_id**：按标签偏移（Label1 的 prop 用 1-999，Label2 用 1001-1999），消除冲突但浪费 ID 空间
- **方案 C — 按标签隔离**：`VertexValue.properties` 改为 `map<LabelId, Properties>`，保留现有 prop_id 方案，访问时按需搜索标签（改动最小）

### 4.4 CREATE 多标签时属性分配

```cypher
CREATE (n:Person:User {name: 'Alice', age: 30})
```

当 Person 和 User 都定义了 `name` 属性时：
- **方案 A**：两个标签都存
- **方案 B**：只存第一个标签
- **方案 C**：支持属性中指定标签归属

---

## 五、实现范围（初步估计）

### 第一阶段：多标签基础修复

- 修复 `LabelScanOp` / `LogicalPlanBuilder`：使用全部 labels
- 修复 `AllNodeScanPhysicalOp`：合并同顶点多标签
- 修复 `VertexValue.properties` 存储模型（方案选择后）
- 修复 `ExpressionEvaluator::evalPropertyAccess` 的碰撞问题
- 统一属性合并逻辑，确保一致性

### 第二阶段：Cypher 语法扩展

- 扩展 ANTLR Grammar：支持转型语法 `n.(Label).prop`
- 扩展 AST：新增 `LabelCastExpr` 或类似节点
- 解析到 `PropertyAccess` 时携带可选的 label 限定符
- 逻辑计划/物理计划传递 label 限定符

### 第三阶段：校验与优化

- 静态校验：转型语法中 Label 属性存在性检查
- 冲突检测与报告（严格模式）
- 索引策略：跨标签属性索引的查询优化
- 冲突解决策略实现
- CREATE 多标签属性分配策略

### 第四阶段：DDL 支持

- `CREATE LABEL` / `DROP LABEL` 对多标签的影响处理
- `SET n:Label` 已解析待执行（需考虑属性合并）
- `REMOVE n:Label` 已解析待执行

---

## 六、影响范围

| 模块 | 影响 |
|------|------|
| Grammar (ANTLR .g4) | 新增转型语法产生式 |
| AST | 新增 LabelCastExpr，PropertyAccess 扩展 label 限定 |
| LogicalPlanBuilder | LabelScanOp 支持多标签，新增转型表达式处理 |
| PhysicalPlanner | 多 LabelId 传递，转型表达式解析 |
| ExpressionEvaluator | 转型属性访问、冲突检测 |
| PhysicalOperator (Scan/Expand) | 多标签属性加载与合并策略 |
| VertexValue / EdgeValue | 存储模型可能变更 |
| KeyCodec / 存储编码 | 影响较小（每标签独立表不变） |
| GraphSchema / MetaStore | 可能需新增标签优先级/继承信息 |
