# 多标签（Multi-Label）查询设计

> 状态：已实现
> 相关模块：Cypher 解析、逻辑计划、物理计划、存储编码、表达式求值

---

## 一、设计动机

当前图数据库在 Schema 设计上存在两个极端：

| 系统 | 问题 |
|------|------|
| **NebulaGraph** | 强 Schema 过度僵硬：属性严格绑定在标签上，跨标签查询实体属性极其繁琐，开发者必须时刻记住属性属于哪个标签 |
| **Neo4j** | 无 Schema/弱 Schema 过度自由：属性是键值对"便利贴"，缺乏静态类型安全保障，核心业务逻辑容易因拼写错误或类型不匹配而静默失败 |

本设计提出一种**双模式查询机制**，在两者之间架设桥梁：

> "日常探索追求便捷（n.name），关键业务逻辑追求类型安全（n::B.name）"

---

## 二、设计方案

### 模式一：自动合并（便捷模式）

```cypher
MATCH (n:TagA) RETURN n.name
```

- 执行引擎从该顶点**所有标签**的属性集中查找 `name`
- 仅一个标签定义了该属性：返回**标量**（如 `"张三"`）
- 多个标签都有同名属性时：合并为**列表**返回（如 `["张三", "张大壮"]`）
- 列表元素按标签扫描顺序排列，顺序不保证
- 开发者不需要知道属性属于哪个标签 — 以实体为中心访问数据

### 模式二：静态转型（类型安全模式）

```cypher
MATCH (n:TagA) RETURN n::TagB.name
```

- `::` 后面必须是**字面量标签名**，不支持动态表达式
- 显式以 TagB 的视角访问属性，限定只在 TagB 的属性中查找
- **编译期**（执行前）：校验 TagB 是否定义了 `name` 属性，没有则报错
- **运行期**：如果顶点 n 没有绑定 TagB，返回 null（类似 LEFT JOIN 语义）

转型语法同样适用于 WHERE 子句：

```cypher
MATCH (n:Person) WHERE n::Employee.salary > 10000 RETURN n.name
```

### 模式三：按标签返回全部属性

```cypher
MATCH (n) RETURN n::TagB
```

- 语义：返回顶点 n 在 TagB 标签下的**所有属性**（以 map/dict 形式）
- 如果 n 没有绑定 TagB，返回 null

### CREATE 语义：类 NebulaGraph 多标签模型

- 每次 `CREATE (n:Label {...})` 时，系统自动分配唯一顶点 ID
- 该顶点与创建时指定的标签实体建立关联（写入 `label_reverse` 表）
- **不支持** `CREATE (n:TagA:TagB)` 和 `CREATE (n:TagA:TagB {prop: val})`（多标签属性分配歧义太大，返回错误）
- 后续通过 `SET n:NewLabel` 给已有顶点增加新标签
- 每个标签的属性独立存储（`vprop_{label_id}` 表），互不干扰
- **标签可以不定义属性列**（`CREATE LABEL TagName`），用于纯标记/分类场景

```cypher
CREATE LABEL Person (name STRING)            -- 有属性标签
CREATE LABEL Employee                        -- 无属性标签（纯关联）
CREATE (n:Person {name: 'Alice'})            -- 生成 vid=1001，关联 Person 标签
SET n:Employee                                -- 增加 vid=1001 → Employee 标签映射
SET n::Employee.salary = 10000               -- 设置 Employee 标签下的属性
```

---

## 三、当前实现

### 3.1 各路径多标签支持

| 路径 | 实现 |
|------|------|
| **解析器（Parser）** | `NodePattern.labels` 是 `vector<string>`，语法已支持 `(n:A:B)` |
| **逻辑计划（LabelScanOp）** | 使用 `labels` 完整列表传递给物理算子 |
| **物理计划（LabelScanPhysicalOp）** | 多 LabelId，加载全部指定标签属性，获取完整 `LabelIdSet` |
| **物理计划（AllNodeScanPhysicalOp）** | 按 `VertexId` 去重，同一顶点多标签合并为一行 |
| **物理计划（ExpandPhysicalOp）** | per-label 属性存储（`map<LabelId, Properties>`） |
| **表达式求值（PropertyAccess）** | 便捷模式遍历所有 label（冲突时合并为列表），转型模式定位指定 label |
| **CREATE** | 限制为单标签，多标签 CREATE 返回错误 |
| **SET/REMOVE** | 支持 `SET n:Label` / `SET n::Label.prop = val` / `REMOVE n:Label` / `REMOVE n.prop` |

### 3.2 属性存储模型

`VertexValue.properties` 类型为 `map<LabelId, Properties>`，每个标签的属性独立存储。prop_id 在每个 Label 内独立编号（从 1 开始），按标签隔离彻底消除了不同标签 prop_id 互相覆盖的问题。

### 3.3 底层基础设施

- 顶点支持 `LabelIdSet`（多标签集合）
- 属性按标签独立存储在 `vprop_{id}` 表（每标签一张表）
- 标签反向索引 `label_reverse` 表维护顶点→标签映射
- `GraphSchema` 维护标签名→ID 双向映射

---

## 四、设计决策

### 4.1 属性冲突解决策略：合并为列表

仅一个标签定义了该属性 → 返回**标量**；多个标签都有同名属性 → 合并为**列表**返回。

- 优点：最灵活，不丢信息，大多数场景下单标签命中返回标量
- 缺点：同一属性名在不同场景下返回类型可能不同，调用方需处理

### 4.2 转型语法：`n::B.name`

采用 `::` 作用域限定符，接近 C++ 命名空间语义。`B` 必须是**字面量标签名**，不支持动态表达式。

### 4.3 属性存储模型：按标签隔离

`VertexValue.properties` 为 `map<LabelId, Properties>`。每个标签的属性独立存储和访问，兼容现有 `vprop_{id}` 表结构。表达式求值在转型模式下直接定位到对应 LabelId 的 Properties，便捷模式下遍历所有 LabelId。

### 4.4 CREATE 限制为单标签

不支持 `CREATE (n:TagA:TagB {...})`（多标签属性分配歧义太大）。多标签关联通过后续 `SET n:Label` 添加。标签可以不定义属性列，用于纯标记/分类场景。

---

## 五、涉及的模块

| 模块 | 变更 |
|------|------|
| Grammar (ANTLR .g4) | 新增 `::` 转型语法产生式 |
| AST | 新增 `LabelCastExpr` |
| LogicalPlanBuilder | LabelScanOp 支持多标签；新增 SetOp/RemoveOp |
| PhysicalPlanner | 多 LabelId 传递；新增 SetPhysicalOp/RemovePhysicalOp |
| ExpressionEvaluator | 转型属性访问、便捷模式冲突合并为列表 |
| PhysicalOperator (Scan/Expand/Set/Remove) | 多标签属性加载与 per-label 存储 |
| VertexValue | properties 改为 `map<LabelId, Properties>` |
| IAsyncGraphDataStore | 新增 addVertexLabel/removeVertexLabel/putVertexProperty 等接口 |
| SyncGraphDataStore | insertVertex 适配；getVertexProperties 返回格式 |
