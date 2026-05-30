# 图数据库混合模式：标签作用域属性访问设计

## 1. 设计动机与核心问题

在强弱混合模式的图数据库中，一个实体可同时拥有多个标签（包括用于兜底弱模式的隐藏标签），但属性在不同标签间并无物理隔离。这导致数据操纵语言（DML）在写入属性时，面临"属性归属不清"的严重歧义。

**核心矛盾**：当执行 `SET n.new_prop = value` 时，系统无法判断该属性应属于节点 `n` 的哪一个标签，或是应该落入弱模式的"默认无标签空间"。

为解决此问题，我们引入标签作用域限定符 `::`，允许在所有 DML 语句中显式指定属性的宿主标签，确保语义清晰、Schema 约束可严格执行。

### 1.1 EuGraph 存储模型

EuGraph 采用 WiredTiger 多表隔离设计。每个标签（Label）拥有独立的属性表 `vprop_{id}`，属性以 `{vertex_id:u64BE}{prop_id:u16BE}` 为 key 逐条存储。`prop_id` 在每个标签内独立编号、从 1 开始、永不复用。属性名与 `prop_id` 的映射保存在元数据 `LabelDef` 的 `PropertyDef` 列表中。

边（Edge）同样按边类型（EdgeLabel）隔离存储在 `eprop_{id}` 表中，机制与顶点一致。

### 1.2 弱模式：`__anon__` 隐藏标签

当前实现中，无标签顶点（如 `CREATE (n {name: 'Alice'})`）被分配一个内部隐藏标签 `__anon__`：

- `GraphManager::openGraphInstance()` 启动时自动创建 `__anon__` 标签（`src/storage/graph_manager.cpp:220-233`）
- `Catalog::load()` 将 `__anon__` 从用户可见的标签映射中隐藏（`src/query/catalog/catalog.cpp:16-21`）
- Binder 对无标签的 CREATE 节点自动使用 `catalog_.getAnonLabelId()`（`src/query/planner/binder/bind_mutation.cpp:33`）

### 1.3 现有的便捷模式（无 `::` 时的读取行为）

当前实现中，`n.name`（无 `::`）采用便捷模式读取：

- Binder 通过 `lookupPropertyAcrossLabels(all_labels, "name")` 搜索所有标签的属性定义
- 仅一个标签定义了该属性 → 返回**标量**
- 多个标签都有同名属性 → 合并为**列表**返回
- 所有标签都没有该属性 → 回退到 `BoundDynamicPropertyRef`（运行时按名匹配）
- 编译期类型推断：所有候选类型一致则推导为具体类型，否则为 `ANY`

详见 `docs/features/multi-label-design.md` 和 `docs/features/unlabeled-node-property-design.md`。

### 1.4 `::` 的作用：消除歧义 + 精确读写

便捷模式在**读取**时表现良好（自动搜索、合并），但**写入**时可能产生歧义。`::` 解决此问题：

**写入规则（无 `::`）**——运行时自动推断归属：

在运行时根据节点**实际持有的标签**判断：

1. 恰好一个标签的 schema 包含该属性 → 写入该标签的 `prop_id` 存储
2. 多个标签都有该属性 → **运行时报错**，需要 `::` 显式指定
3. 没有任何标签包含该属性 → 回退到 `__anon__`（轻量动态分配 `prop_id`）

```cypher
-- User schema 有 id 和 name → 运行时命中 User，写入 User 的 prop_id 存储
CREATE (new:User {id: 'target_id', name: 'Unknown'})

-- nickname 不在任何标签的 schema 中 → 无标签命中，写入 __anon__
CREATE (new:User {id: '1', name: 'Alice', nickname: 'Ali'})

-- User 和 Employee 都有 name → 运行时检测到冲突，报错
CREATE (n:User:Employee {name: 'Alice'})
-- RuntimeError: Ambiguous property 'name' found in multiple labels: User, Employee.
-- Use User::name or Employee::name to specify.
```

**写入规则（有 `::`）**——显式指定：

- 属性操作限定在指定标签的 schema 空间
- Binder 校验属性是否存在于 LabelDef，不存在则报错
- Schema 变更通过独立 DDL 语句完成

**读取规则**：保持现有便捷模式行为不变（跨所有标签搜索，单标签返回标量，多标签合并为列表）。

### 1.5 设计约束

- **统一存储格式**：所有标签（包括 `__anon__`）都使用 `prop_id` KV 存储，不引入第二种存储形态
- **边保持强模式**：边必须有预定义的 EdgeLabel，属性按 `prop_id` 存储。不引入边的弱模式
- **`AlterVertexLabelPhysicalOp` 已删除**：所有顶点 pending_props 统一走 `__anon__` 轻量路径，不再需要顶点 DDL 算子。边的 `AlterEdgeLabelPhysicalOp` 保留。

## 2. `__anon__` 弱模式：轻量级动态属性管理

### 2.1 核心思路

`__anon__` 的属性管理遵循一个简单的原则：**添加属性只是为属性名分配一个 `prop_id`，没有副作用**。

强模式标签添加属性时，可能涉及：
- Schema 约束校验（required、类型检查）
- 索引维护（新属性需要回填索引）
- 默认值处理

而 `__anon__` 没有以上任何负担，因此可以安全地使用轻量级并发机制管理其属性名 → `prop_id` 映射。

### 2.2 实现方案

`__anon__` 标签的 `LabelDef` 仍然存在，但其 `PropertyDef` 的变更走轻量路径：

- 使用并发安全的 map（如 `concurrent_hash_map` 或带锁的 `unordered_map`）管理 `__anon__` 的属性名 → `prop_id` 映射
- 首次遇到新属性名时，分配新的 `prop_id`（递增），写入映射，并异步持久化到元数据
- 不需要在 DML 事务中执行完整的 ALTER 流程（无需 `AlterVertexLabelPhysicalOp`，该算子已删除）
- 实际的属性存储仍然使用 `vprop_{__anon_id}` 表的 `{vertex_id}{prop_id}` 格式

### 2.3 读写路径

`__anon__` 的实际存储路径与所有其他标签完全一致，没有特殊分支：

| 操作 | 路径 |
|------|------|
| 写入 `SET n.name = 'x'`（无 `::`，无标签匹配） | 查找 `__anon__` 的属性名 → `prop_id` 映射，若不存在则轻量分配新 `prop_id` → 按 `(vertex_id, prop_id)` 写入 |
| 删除 `REMOVE n.name`（无 `::`，无标签匹配） | 查找 `__anon__` 映射获取 `prop_id` → 按 `(vertex_id, prop_id)` 删除 |
| 创建 `CREATE (n {...})`（无标签） | 为新属性名分配 `prop_id` → 逐条写入 `vprop_{__anon_id}` |

注意：无 `::` 的属性写入在运行时根据节点实际持有的标签推断归属，先匹配实际标签的 schema，无匹配时才回退到 `__anon__`。详见第 3 节。

## 3. Binder 行为

### 3.1 强模式（`::Label`）：编译期校验

当属性访问使用 `::Label` 限定时（如 `n::User.name`），Binder 通过 `Catalog::lookupProperty(LabelId, prop_name)` 查找 `prop_id`：

- **找到**：产出 `BoundPropertyRef`，携带单个候选 `(label_id, prop_id)`，编译期类型从 `PropertyDef.type` 推导
- **未找到**：**编译期报错**，提示属性不存在于该标签的 schema 中

```cypher
-- User 标签没有 salary 属性 → binder 阶段直接报错
SET n::User.salary = 5000
-- Error: Property 'salary' does not exist in label 'User'.
-- Use DDL to add the property, or use n.salary (without ::) to write to the weak mode space.
```

### 3.2 便捷模式（无 `::`）：保持现有行为

当属性访问没有 `::` 限定时（如 `n.name`），保持现有的便捷模式行为：

**读取**（RETURN / WHERE / ORDER BY 等）：

- Binder 通过 `lookupPropertyAcrossLabels(all_labels, prop_name)` 搜索所有标签（包括 `__anon__`）
- 找到候选 → 产出 `BoundPropertyRef`，携带所有匹配标签的 `(label_id, prop_id)` 候选列表
  - 仅一个标签命中 → 运行时返回标量
  - 多个标签命中 → 运行时合并为列表
- 无候选 → 产出 `BoundDynamicPropertyRef`（运行时按名匹配，兜底 `__anon__`）

**写入**（SET / CREATE 等）——运行时自动推断归属：

- Binder 产出"自动推断写入"算子，不做归属判断
- 运行时根据节点**实际持有的标签**推断：遍历实际标签的 schema，查找哪些标签定义了该属性
- 恰好一个标签包含该属性 → 写入该标签的 `prop_id` 存储
- 多个标签都包含该属性 → **运行时报错**，要求使用 `::`
- 没有标签包含该属性 → 写入 `__anon__`（轻量动态分配 `prop_id`）

### 3.3 `::` 的作用总结

| 场景 | 有 `::` | 无 `::` |
|------|---------|---------|
| 读取 | 直接定位到指定标签的 `prop_id` | 搜索所有标签，单标签返回标量，多标签合并为列表 |
| 写入 | 写入指定标签，属性不存在则 binder 报错 | 运行时推断：单标签命中→该标签，多标签命中→运行时报错，无命中→`__anon__` |
| 类型 | 从 `PropertyDef.type` 推导具体类型 | 候选类型一致则推导具体类型，否则 `ANY` |

## 4. 核心语法：`::` 标签作用域限定符

**基本形式**：`节点变量::标签名.属性名`

**语义**：明确指定后续的属性操作，是在该特定标签的 Schema 视角下进行的。

该语法在读取和写入时对称使用，保持语言的一致性。

### 4.1 读取时的标签转型

延续转型查询思想，`::` 也可用于明确读取特定标签下的属性：

```cypher
RETURN n::Employee.name, n::User.name
```

### 4.2 类型系统设计

**强模式** `n::Label.prop`：

- 编译期通过 `PropertyDef.type` 推导具体类型
- 编译期检查属性是否存在于 LabelDef
- 若不存在，binder 阶段直接报错

**便捷模式** `n.prop`（无 `::`）：

- 编译期通过 `lookupPropertyAcrossLabels` 收集所有标签的候选类型
- 所有候选类型一致 → 推导为该具体类型
- 候选类型不一致 → 类型为 `ANY`
- 无候选 → 类型为 `ANY`，运行时通过 `BoundDynamicPropertyRef` 按名匹配

```cypher
-- 强模式：编译期类型从 User schema 推导（如 STRING）
RETURN n::User.name

-- 便捷模式：编译期跨所有标签收集候选类型
-- 若所有候选类型一致则推导为具体类型，否则为 ANY
MATCH (n:User:Employee)
RETURN n.age
```

## 5. DML 语句设计详解

### 5.1 SET 语句：设置属性

**旧有问题**：

```cypher
MATCH (n {id: 'some_id'})
SET n.nickname = 'Tom'
```

歧义：`nickname` 该存入 `User` 标签、`Employee` 标签，还是弱模式的隐藏标签？

**新语法与语义**：

```cypher
-- 强模式：明确将 nickname 属性写入节点的 User 标签下
-- Binder 检查 User schema，若 nickname 不存在则报错
MATCH (n {id: 'some_id'})
SET n::User.nickname = 'Tom'
```

```cypher
-- 便捷模式：省略标签限定，自动推断归属
-- 若恰好一个标签的 schema 包含 nickname → 写入该标签
-- 若没有标签包含 → 写入 __anon__（轻量分配 prop_id）
-- 若多个标签包含 → 歧义报错
MATCH (n {id: 'some_id'})
SET n.nickname = 'Tom'
```

**语义解析**：引擎收到 `SET n::User.nickname = 'Tom'` 后，会直接检查 `User` 标签的 Schema：

- 若 Schema 中存在该属性，则校验类型并写入（按 `prop_id` KV 路径）。
- 若 Schema 中不存在，则 **binder 阶段报错**，提示用户通过 DDL 添加属性或使用弱模式。

引擎收到 `SET n.nickname = 'Tom'`（无 `::`）后，在**运行时**根据节点实际持有的标签判断：

- 遍历该节点实际拥有的标签，查找哪些标签的 schema 定义了 `nickname`
- 恰好一个标签 → 写入该标签的 `prop_id` 存储
- 没有标签 → 查找/分配 `__anon__` 的 `prop_id`，写入 `vprop_{__anon_id}` 表
- 多个标签 → **运行时报错**，要求使用 `::`

### 5.2 REMOVE 语句：移除属性

**旧有问题**：

```cypher
MATCH (n:User:Employee {id: 'some_id'})
REMOVE n.temp_field
```

歧义：若 `User` 和 `Employee` 标签下都有 `temp_field`，此语句会无差别删除所有，可能造成数据丢失。

**新语法**：

```cypher
-- 强模式：仅删除 User 标签下的 temp_field（按 prop_id）
MATCH (n:User:Employee {id: 'some_id'})
REMOVE n::User.temp_field
```

```cypher
-- 便捷模式：运行时遍历节点实际标签，删除所有标签下的 temp_field
-- 无标签命中→删除 __anon__ 下的
REMOVE n.temp_field
```

**语义解析**：精准删除。强模式按 `(vertex_id, prop_id)` 精确删除指定标签下的属性（Binder 校验）；便捷模式在运行时遍历节点实际标签，删除所有拥有该属性的标签下的对应属性，无命中则删除 `__anon__` 下的。与 SET 不同，REMOVE 不存在冲突问题——删除所有匹配是安全的、幂等的。

### 5.3 REMOVE n:Label 语句：移除标签

`REMOVE n:User` 会删除该节点的 `User` 标签记录，同时级联删除该标签下的所有属性（`prop_id` KV 存储）。`__anon__` 的属性不受影响。

**语义模型**：标签是一个属性容器（或一条"子记录"）。

| 操作 | 语义 |
|------|------|
| `REMOVE n:User` | 删除该节点上 `User` 标签的整条记录（标签本身 + 其下所有 `prop_id` 属性） |
| `REMOVE n::User.name` | 仅删除 `User` 标签下的 `name` 属性（按 `prop_id`），保留标签和其他属性 |
| `REMOVE n.name`（无 `::`） | 便捷模式：运行时遍历节点实际标签，删除所有标签下的 `name` 属性；无匹配则删除 `__anon__` 下的 |

#### 边界情况

**(1) 删除标签后，节点还存在吗？**

```cypher
CREATE (n:User {name: 'Alice'})
REMOVE n:User
```

节点 `n` 仍然存在，只是没有了 `User` 标签和其 `prop_id` 属性。它会变成一个仅有 `__anon__` 属性空间的节点。如果 `__anon__` 空间也为空，则节点无任何属性。

**(2) 多标签场景下，只删除一个标签**

```cypher
CREATE (n:User:Employee {
  User::name: 'Alice',
  Employee::salary: 5000
})
REMOVE n:User
```

结果：

- `User` 标签及其 `prop_id` 属性 `name` 被删除
- `Employee` 标签及其 `prop_id` 属性 `salary` 保留
- 节点 `n` 仍然存在，标签变为 `:Employee`
- `__anon__` 属性空间不受影响

**(3) 如果属性在多个标签中都存在？**

```cypher
CREATE (n:User:Employee {
  User::name: 'Alice',
  Employee::name: 'Alice Worker'
})
REMOVE n:User
```

`User::name` 被删除，`Employee::name` 保留。`RETURN n::Employee.name` 返回 `'Alice Worker'`。

注意：此时 `RETURN n.name`（无 `::`）读取的是 `__anon__` 属性空间的值。如果 `__anon__` 中没有 `name`，则返回 `null`。

**(4) 约束/索引的交互**

如果 `User` 标签上有唯一索引：

```cypher
CREATE INDEX FOR (n:User) ON (n.name)
```

当 `REMOVE n:User` 删除标签记录时，索引中对应的条目也需要同步删除。

### 5.4 CREATE 语句：创建节点并赋予属性

**旧有问题**：

```cypher
CREATE (n:User:Employee {name: 'Alice', salary: 1000})
```

歧义：`name` 和 `salary` 分别属于哪个标签？

**新语法**：

```cypher
-- 强模式：属性通过 :: 明确归属，Binder 检查各标签 Schema
CREATE (n:User:Employee {
  User::name: 'Alice',
  Employee::salary: 1000
})
```

```cypher
-- 弱模式：无标签，所有属性存入 __anon__
-- 遇到新属性名时轻量分配 prop_id
CREATE (n {name: 'Alice', age: 30})
```

**混合写法**：

```cypher
-- id 和 name 在 User schema 中 → 写入 User 的 prop_id 存储
-- nickname 不在任何标签 schema 中 → 写入 __anon__ 的 prop_id 存储
CREATE (n:User {
  User::name: 'Alice',
  nickname: 'Ali'
})

-- 等价的简化写法（运行时推断：id/name 命中 User，nickname 无命中→__anon__）
CREATE (n:User {id: '1', name: 'Alice', nickname: 'Ali'})
```

**语义解析**：`::` 前缀的属性走强模式路径（Binder 校验 `prop_id`），无 `::` 前缀的属性走便捷模式（运行时推断归属标签，单标签命中→该标签，无命中→`__anon__`，多标签命中→运行时报错）。

### 5.5 MERGE 语句：合并创建或匹配

**旧有问题**：

```cypher
MERGE (n:User {id: 'some_id'})
ON CREATE SET n.name = 'Alice', n.created_at = timestamp()
ON MATCH SET n.last_seen = timestamp()
```

歧义：`SET n.name` 的属性归属不明确。

**新语法**：

```cypher
-- 强模式：所有属性通过 :: 指定到 User 标签
MERGE (n:User {id: 'some_id'})
ON CREATE SET n::User.name = 'Alice', n::User.created_at = timestamp()
ON MATCH SET n::User.last_seen = timestamp()
```

```cypher
-- 便捷模式：运行时根据节点实际标签自动推断归属
-- 恰好一个标签有该属性 → 写入该标签
-- 无标签有该属性 → 写入 __anon__
-- 多个标签有该属性 → 运行时报错，提示使用 ::
MERGE (n:User {id: 'some_id'})
ON CREATE SET n.name = 'Alice', n.created_at = timestamp()
ON MATCH SET n.last_seen = timestamp()
```

### 5.6 MATCH 中隐式创建点

当 MATCH 未命中时，通过子查询动态构造节点并赋予属性：

**旧有问题**：

```cypher
OPTIONAL MATCH (n:User {id: 'target_id'})
WITH n
CALL {
  WITH n
  WITH n WHERE n IS NULL
  CREATE (new:User {id: 'target_id', name: 'Unknown', is_placeholder: true})
  RETURN new AS n
}
RETURN n
```

歧义：`id`、`name`、`is_placeholder` 都扁平地赋予 `User` 标签，若 `is_placeholder` 应该属于另一个标签 `:SystemMeta`，则无法表达。且如果 `User` 是强模式而 `is_placeholder` 不在 schema 中，当前实现会触发运行时 ALTER。

**新语法**：

```cypher
-- 强模式：属性通过 :: 显式归属，Binder 校验各标签 schema
OPTIONAL MATCH (n:User {id: 'target_id'})
WITH n
CALL {
  WITH n
  WITH n WHERE n IS NULL
  CREATE (new:User:SystemMeta {
    User::name: 'Unknown',
    SystemMeta::is_placeholder: true
  })
  RETURN new AS n
}
RETURN n
```

在新设计中，`User::name` 由 Binder 检查 User schema，`SystemMeta::is_placeholder` 由 Binder 检查 SystemMeta schema。不存在的属性在 binder 阶段即报错（而非运行时 ALTER）。

```cypher
-- 便捷模式：运行时根据节点实际标签自动推断归属
-- 恰好一个标签有该属性 → 写入该标签
-- 无标签有该属性 → 写入 __anon__
-- 多个标签有该属性 → 运行时报错
CREATE (new:User:SystemMeta {name: 'Unknown', is_placeholder: true})
```

## 6. 扩展操作符的标签作用域

### 6.1 属性替换操作符 `SET n = {...}`

**旧有问题**：

```cypher
MATCH (n:User {id: 'some_id'})
SET n = {name: 'Tom', city: 'London'}
```

歧义：替换哪个标签的属性？新属性归属哪里？

**新语法（支持多种方式）**：

**方式一：多条 SET 子句，以标签为中心**（适合整体替换单个标签的全部属性）

```cypher
MATCH (n {id: 'some_id'})
SET n::User = {name: 'Tom', city: 'London'}
SET n::Employee = {salary: 6000}
```

语义：将 `User` 标签下的属性替换为 `{name: 'Tom', city: 'London'}`（删除旧的 `prop_id` 条目，写入新的）。`__anon__` 属性空间不受影响。Binder 会校验 `city` 是否存在于 User schema。

**方式二：单条 SET 子句，以属性为中心**（适合精确设置跨标签的特定属性）

```cypher
MATCH (n:User {id: 'some_id'})
SET n = {::User.name: 'Tom', ::User.city: 'London'}
```

语义：将指定标签下的指定属性设置为新值，并删除该标签下的其他旧属性。`::` 前缀的属性走强模式（Binder 校验），无 `::` 前缀的走便捷模式（运行时自动推断：单标签命中→该标签，无命中→`__anon__`，多标签命中→运行时报错）。

### 6.2 属性合并操作符 `SET n += {...}`

**新语法**：

```cypher
-- 强模式：批量更新 Employee 标签的 prop_id 属性，Binder 校验属性是否存在于 schema
MATCH (n:User:Employee {id: 'some_id'})
SET n::Employee += {salary: 5000, bonus: 1000}
```

```cypher
-- 便捷模式：运行时根据节点实际标签自动推断归属
-- 保留已有 prop_id 条目，新增/更新指定的
-- 单标签命中→该标签，无命中→__anon__，多标签命中→运行时报错
MATCH (n:User:Employee {id: 'some_id'})
SET n += {salary: 5000, bonus: 1000}
```

### 6.3 动态属性访问 `n[propName]`

**新语法**：

```cypher
-- 强模式：先限定标签，再动态访问 prop_id，Binder 校验属性是否存在于该标签 schema
RETURN n::Employee["salary"]
```

```cypher
-- 便捷模式：运行时根据节点实际标签自动推断
-- 单标签命中→该标签，无命中→__anon__，多标签命中→运行时报错
RETURN n["salary"]
```

## 7. ORDER BY 与聚合函数的歧义处理

**设计原则**：便捷模式（无 `::`）下，多标签同名属性合并为列表。但 ORDER BY 和聚合函数需要标量输入，列表会导致语义不确定。此时应通过 `::` 显式指定使用哪个标签下的属性。

### 7.1 ORDER BY 的歧义

```cypher
MATCH (n:User:Employee)
ORDER BY n.created_at
```

若 `created_at` 在 User 和 Employee 标签下都存在，便捷模式会合并为列表，但 ORDER BY 需要标量，应报错：

```
Ambiguous property 'created_at' found in multiple labels: User, Employee.
Use n::Label.created_at to specify.
```

正确写法：

```cypher
MATCH (n:User:Employee)
ORDER BY n::User.created_at
```

### 7.2 聚合函数的歧义

```cypher
MATCH (n:User:Employee)
RETURN avg(n.salary)
```

若 `salary` 在多个标签下都存在，聚合函数需要标量输入，同样报错：

```
Ambiguous property 'salary' found in multiple labels: User, Employee.
Use n::Label.salary to specify.
```

正确写法：

```cypher
MATCH (n:User:Employee)
RETURN avg(n::Employee.salary)
```

### 7.3 非歧义情况正常通过

如果节点只有一个标签有该属性，或者用户已经用 `::` 限定，则不报错：

```cypher
-- 假设只有 Employee 标签有 salary
MATCH (n:User:Employee)
RETURN avg(n.salary)  -- 正常，便捷模式只命中一个标签，返回标量
```

```cypher
-- 即使多标签有同名属性，但用户显式指定了
MATCH (n:User:Employee)
ORDER BY n::User.created_at  -- 正常
```

### 7.4 边界说明

**空值 vs 歧义**：

```cypher
MATCH (n:User:Employee)
RETURN avg(n.salary)
```

如果某个节点有 `Employee` 标签但没有 `salary` 属性，这不是歧义问题，而是属性缺失（返回 `null`，聚合函数跳过）。

**DISTINCT 聚合**：

```cypher
MATCH (n:User:Employee)
RETURN collect(DISTINCT n::Employee.department)
```

## 8. LOAD CSV 与批量导入

`::` 语法在批量导入场景下可以直接复用，无需额外发明新语法。

### 8.1 基础导入：CSV 列直接映射到不同标签

CSV 文件内容：

```csv
name,age,salary,dept
Alice,30,5000,Engineering
Bob,25,4000,Marketing
```

导入语句：

```cypher
LOAD CSV FROM 'file:///employees.csv' WITH HEADER AS row
CREATE (n:User:Employee {
  User::name: row.name,
  User::age: toInteger(row.age),
  Employee::salary: toInteger(row.salary),
  Employee::dept: row.dept
})
```

### 8.2 弱模式导入

```cypher
LOAD CSV FROM 'file:///people.csv' WITH HEADER AS row
CREATE (n {name: row.name, age: toInteger(row.age)})
```

所有属性存入 `__anon__`，遇到新属性名时轻量分配 `prop_id`。

### 8.3 批量更新：CSV 驱动 MERGE + 标签作用域

```cypher
LOAD CSV FROM 'file:///updates.csv' WITH HEADER AS row
MERGE (n:User {User::id: toInteger(row.id)})
ON MATCH SET n::User.name = row.name,
             n::Employee.salary = toInteger(row.salary)
```

### 8.4 未来可能的语法糖：标签分组的 Map 字面量

可选的更简洁写法，核心语义仍基于 `::`：

```cypher
LOAD CSV FROM 'file:///employees.csv' WITH HEADER AS row
CREATE (n:User:Employee {
  User: {name: row.name, age: toInteger(row.age)},
  Employee: {salary: toInteger(row.salary), dept: row.dept}
})
```

## 9. 索引设计

本设计不支持 Neo4j 风格的约束语法（`CREATE CONSTRAINT ... ASSERT EXISTS/IS UNIQUE`），而是直接为特定标签创建唯一索引。

```cypher
CREATE INDEX FOR (n:User) ON (n.name)
```

索引天然就是"标签作用域"的，`FOR (n:Label)` 已经明确了作用域，因此 `::` 语法体系在索引侧无需额外处理。

**注意**：索引仅适用于强模式标签的 `prop_id` 属性。`__anon__` 中的属性不支持索引。

## 10. 旧有问题总览表

| 语句/操作符 | 旧问题语句示例 | 核心歧义 |
|---|---|---|
| `SET` | `SET n.nickname = 'Tom'` | 属性该写入哪个标签？ |
| `REMOVE` | `REMOVE n.temp_field` | 删除的是哪个标签下的属性？ |
| `REMOVE n:Label` | `REMOVE n:User` | 属性是级联删除还是保留？ |
| `CREATE` | `CREATE (n:User:Employee {name: 'Alice', salary: 1000})` | 属性如何分配给不同标签？ |
| `MERGE` | `ON CREATE SET n.name = 'Alice'` | 分支中的属性归属不明 |
| `MATCH` 内创建 | `CREATE (new:User {is_placeholder: true})` | 动态构造时属性绑定模糊 |
| `SET n = {...}` | `SET n = {name: 'Tom', city: 'London'}` | 替换哪个标签的属性？新属性落哪？ |
| `SET n += {...}` | `SET n += {salary: 5000}` | 新增属性归属哪个标签？ |
| 动态属性访问 `n[key]` | `n["salary"]` | 多标签同名属性时返回哪个？ |
| `ORDER BY` | `ORDER BY n.created_at` | 多标签同名属性时按哪个排序？ |
| 聚合函数 | `avg(n.salary)` | 多标签同名属性时聚合哪个？ |

## 11. 现有代码需调整的清单

### 11.1 元数据层 — `__anon__` 轻量动态属性

| 变更 | 文件 | 说明 |
|------|------|------|
| `__anon__` 并发属性映射 | `src/storage/meta/async_graph_meta_store.cpp` | `__anon__` 的属性名 → `prop_id` 映射使用并发安全结构，首次遇到新属性名时轻量分配 `prop_id`，不需要完整 ALTER 流程 |
| 持久化 | 同上 | `__anon__` 的 PropertyDef 变更异步持久化到元数据（不阻塞 DML 事务） |

### 11.2 查询引擎 — Binder

| 变更 | 文件 | 说明 |
|------|------|------|
| 强模式编译期报错 | `src/query/planner/binder/bind_expression.cpp` | `::Label` 路径中 `lookupProperty()` 返回 null 时直接报错 |
| 强模式 SET 校验 | `src/query/planner/binder/bind_mutation.cpp` | `SET n::Label.prop` 时，prop 不在 schema 中则报错 |

### 11.3 查询引擎 — 物理算子

| 变更 | 文件 | 说明 |
|------|------|------|
| 移除 `__anon__` 的 ALTER | `src/query/physical_plan/physical_planner.cpp` | `AlterVertexLabelPhysicalOp` 已删除。所有顶点 pending_props 统一通过 `CreateNodePhysicalOp` 内部 `getOrCreateAnonPropId()` 轻量分配 `prop_id` |
| `__anon__` 属性写入 | `src/query/physical_plan/operator/create_node_physical_op.cpp` | 逐行创建：`__anon__` 及所有标签的 pending_props 在首次执行时通过 `getOrCreateAnonPropId()` 分配 prop_id，运行时动态 VID |
| `__anon__` SET/REMOVE | `src/query/physical_plan/operator/set_physical_op.cpp` | 对 `__anon__` 属性的 SET/REMOVE 操作通过并发映射获取 `prop_id` |

### 11.4 查询引擎 — 属性求值

| 变更 | 文件 | 说明 |
|------|------|------|
| 弱模式属性查找 | `src/query/evaluator/eval_property.cpp` | 便捷模式（无 `::`）遍历所有标签的候选 `(label_id, prop_id)` 返回值；`evalDynamicPropertyRef()` 兜底运行时按名匹配 |

## 12. 与旧方案对比：AlterVertexLabelPhysicalOp 的问题

旧方案中，所有不在标签 schema 中的属性（包括 `__anon__` 节点的所有属性）都通过 `AlterVertexLabelPhysicalOp` 处理。该算子已在重构中删除，所有顶点 pending_props 改走 `__anon__` 轻量路径。以下为历史问题记录，供参考。

### 12.1 ALTER 的适用场景

| 场景 | ALTER 是否合适 | 原因 |
|------|--------------|------|
| DDL 有意添加列 | 合适 | 用户显式要求 schema 变更 |
| `__anon__` 节点的属性 | 不合适 | 没有约束需求，走轻量分配即可 |
| MATCH 内临时属性 | 不合适 | 全局副作用，污染 schema |
| 需要强约束的属性 | 不合适 | 应通过 DDL 显式添加，不应由 DML 隐式触发 |

### 12.2 ALTER 的根本问题

1. **永久修改全局 schema**：`is_placeholder` 被永久添加到 User 的 `LabelDef`。所有 User 节点都获得该属性槽位，即使绝大多数节点不需要。不可逆的 schema 污染。

2. **并发冲突风险**：两个并发查询同时 CREATE 同一标签的未知属性，同时修改共享的 `label_defs` map。

3. **对 `__anon__` 尤其浪费**：`CREATE (n {name: 'Alice'})` 的 `name` 属性走完整 ALTER 流程（同步持久化元数据），仅仅是为了分配一个 prop_id。`__anon__` 没有约束需求，不需要任何校验。

4. **不适合临时/动态属性场景**：MATCH 内动态构造节点时使用的辅助属性（如 `is_placeholder`、`temp_field`）本质上是弱模式需求，ALTER 把它们当作强模式 schema 扩展处理，用重操作解决轻需求。

新设计将三类场景分开：强模式走 DDL、`__anon__` 走轻量分配、便捷模式走运行时推断。

## 13. 设计评估：优劣与可行性

### 13.1 优势

- **`__anon__` 轻量化是最大收益**：无标签节点的属性操作不再走重型 DDL 路径，性能提升明显
- **强模式编译期校验**：`SET n::User.salary = 5000` 如果 User 没有 salary，binder 直接报错，不用等到运行时
- **运行时歧义报错优于静默跳过**：旧方案中 `SET n.prop = val` 如果属性在 0 或 >1 个标签中存在，静默跳过——用户以为写进去了，实际没有。新设计明确报错更安全
- **双模式语义清晰**：显式用 `::` 走严格校验，不用就自动推断，日常使用简洁

### 13.2 与旧行为的关键差异

| 操作 | 旧行为 | 新行为 | 影响 |
|------|--------|--------|------|
| SET 无 `::`，属性不在任何标签 | 静默跳过（什么都不做） | 写入 `__anon__` | 行为变更：属性确实被设置了 |
| SET 无 `::`，属性在多个标签 | 静默跳过 | 运行时报错 | 行为变更：从静默变为报错 |
| REMOVE 无 `::`，属性在多个标签 | 静默跳过 | 删除所有匹配标签下的属性 | 行为变更：从静默变为实际执行 |
| CREATE `__anon__` 节点属性 | 重型 ALTER 路径 | 轻量并发分配 | 纯优化，无行为差异 |

### 13.3 需要注意的风险

1. **`__anon__` 兜底可能导致属性散落**：`SET n.nickname = 'Tom'` 拼错或用了不存在的属性名，不会报错而是静默写入 `__anon__`。用户可能以为写入了 User 标签，实际写入了隐藏空间。读取时 `n.nickname` 能读到，但语义上可能不是用户期望的。

2. **与未来 DDL 的交互**：属性先通过便捷模式写入 `__anon__`，后来用户通过 DDL 给标签添加同名属性，读取行为会突然变化——从 `__anon__` 切换到标签 schema。这种"隐形切换"可能让用户困惑。

3. **运行时推断有性能开销**：便捷模式每行都要遍历节点实际标签做匹配，编译期无法优化。对批量写入场景可能有影响，但标签数量有限，实际开销很小。

### 13.4 可行性结论

设计可行。核心变更集中在 5 个阶段、14 个文件，不涉及存储格式变更（所有标签仍使用 `prop_id` KV 存储）。最关键的依赖是 `__anon__` 轻量分配机制（Phase 1），其余变更均建立在此基础上。

排除尚未实现的功能（MERGE、LOAD CSV、`SET n = {...}` 等），本次重构范围可控。

## 14. 设计优势总结

- **统一存储格式**：所有标签（包括 `__anon__`）都使用 `prop_id` KV 存储，读写路径只有一条，无特殊分支
- **弱模式轻量化**：`__anon__` 添加属性只是分配 `prop_id`，通过并发映射管理，不涉及重型 DDL 事务
- **消除歧义，语义自文档化**：通过 `n::Tag.prop` 显式消除多标签歧义；无 `::` 时自动推断归属，日常使用简洁自然
- **保障 Schema 约束执行**：强模式标签的属性在 binder 阶段校验，编译期即可发现错误
- **读写对称，概念统一**：`::` 统一用于读取（标签转型）和写入（标签指定），降低学习曲线
- **灵活与严格并存**：便捷模式（无 `::`）提供自动跨标签搜索的灵活性，`::` 模式提供精确的标签作用域控制
- **导入场景零学习成本**：`LOAD CSV` 等批量导入直接复用 `::` 语法
- **歧义安全**：`ORDER BY` 和聚合函数在遇到多标签同名属性时报错，强制用户显式指定
