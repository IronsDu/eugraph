# Cypher 语法参考

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

本文档描述 EuGraph 当前支持的 Cypher 语法。标注为「仅解析」表示 ANTLR 能正确解析但执行层尚未实现。

---

## 一、读查询

### MATCH — 图匹配

```cypher
MATCH (n:Label)            -- 按标签扫描顶点
MATCH (n)                  -- 全顶点扫描（无标签过滤）
MATCH (a)-[:KNOWS]->(b)    -- 按关系类型展开
MATCH (a)-[:KNOWS|FRIEND]->(b)  -- 多关系类型（任意匹配）
MATCH (a)-->(b)            -- 无类型过滤，展开所有边
MATCH (a)<-[:KNOWS]-(b)    -- 反向展开
MATCH (a)-[:KNOWS]-(b)     -- 无向展开
```

**变长路径**：

```cypher
MATCH (a)-[*2..3]->(b)      -- 2 到 3 跳
MATCH (a)-[:KNOWS*1..5]->(b) -- 指定边类型的 1 到 5 跳
MATCH (a)-[*3]->(b)          -- 精确 3 跳
MATCH (a)-[*0..3]->(b)       -- 包含 identity path（0 跳，a=b）
MATCH (a)-[*..]->(b)          -- 无上界展开
MATCH (a)-[:KNOWS|LIVES_IN*1..2]->(b)  -- 多边类型
MATCH p = (a)-[:KNOWS*1..3]->(b)       -- 命名路径变量
MATCH (a)-[e:KNOWS*2]->(b)             -- 命名边变量 → LIST<EDGE>
MATCH (a)-[:KNOWS*1..5 {score: 10}]->(b)  -- 逐跳边属性过滤
MATCH (a)-[:KNOWS*1..5]-(b)                -- 无向变长
```

**变长路径限制**：
- min/max 必须是字面量整数，min >= 0
- 混合固定+变长链且带命名路径（`p = (a)-[:X]->(b)-[:Y*2..3]->(c)`）不支持
- 边属性过滤值仅支持字面常量（不支持表达式或变量引用）

**限制**：
- 仅支持单个 MATCH 子句（多 MATCH 不支持）
- 仅支持单个 pattern part（逗号分隔的多个模式不支持）
- OPTIONAL MATCH 已支持：匹配失败时右侧变量为 NULL（左连接语义）

### WHERE — 过滤

```cypher
WHERE n.age > 30 AND m.city = 'Beijing'
WHERE true AND false
WHERE NOT x
WHERE x IS NULL / x IS NOT NULL
```

**支持的操作符**：

| 类别 | 操作符 | 状态 |
|------|--------|------|
| 比较 | `=`, `<>`, `<`, `>`, `<=`, `>=` | 已实现 |
| 逻辑 | `AND`, `OR`, `NOT` | 已实现 |
| 空判断 | `IS NULL`, `IS NOT NULL` | 已实现 |
| 算术 | `+`, `-`, `*`, `/` | 已实现 |
| 算术 | `%`, `^` | 仅解析 |
| 字符串 | `STARTS WITH`, `ENDS WITH`, `CONTAINS` | 已实现 |
| 列表 | `IN` | 已实现 |
| 逻辑 | `XOR` | 已实现 |

**索引优化**：当 Filter 在 LabelScan 之上且谓词为 `n.prop = literal` 模式，且该属性存在 PUBLIC 索引时，自动使用 IndexScan（还支持 `>`, `>=`, `<`, `<=` 的范围扫描）。

**路径谓词**（可与变长路径配合使用）：

```cypher
MATCH p = (a)-[:KNOWS*1..5]->(b)
WHERE ALL(x IN nodes(p) WHERE x.age > 18)
RETURN p

-- 四种量词：ANY / NONE / SINGLE
WHERE ANY(x IN nodes(p) WHERE x.city = 'Beijing')
WHERE NONE(x IN relationships(p) WHERE x.score < 0)
WHERE SINGLE(x IN nodes(p) WHERE id(x) = 100)
```

量词语义（遵循 Cypher 标准）：ALL 空列表→true，ANY 空列表→false，NONE 空列表→true，SINGLE→恰好一个满足。

### EXISTS — 存在性子查询

```cypher
MATCH (n:Person)
WHERE EXISTS { (n)-[:KNOWS]->(:Person) }
RETURN n

-- 内部模式可含 WHERE 过滤
MATCH (n:Person)
WHERE EXISTS { (n)-[:KNOWS]->(m:Person) WHERE m.name = 'name4' }
RETURN n

-- NOT EXISTS（反存在性）
MATCH (n:Person)
WHERE NOT EXISTS { (n)-[:KNOWS]->(:Person) }
RETURN n

-- 多 EXISTS 与 AND 组合
MATCH (n:Person)
WHERE EXISTS { (n)-[:KNOWS]->(:Person) }
  AND EXISTS { (n)-[:KNOWS]->(:Person) }
RETURN n
```

**裸模式谓词（Bare Pattern Predicate）**：WHERE 子句中的 `(n)-[]->()` 等链式模式会被解析器重写为 `EXISTS { ... }`，等价于显式 EXISTS。匿名起点 `(n)-[]->()` 与具名两节点 `(n)-[]->(m)` 均支持。所有命名变量必须在外层作用域已绑定，否则报 `UndefinedVariable`；纯节点 `(n)` 不构成合法谓词，报 `InvalidArgumentType`。

```cypher
-- 裸模式谓词（等价于 EXISTS）
MATCH (n) WHERE (n)-[:KNOWS]->() RETURN n

-- 两节点关联（n 与 m 都来自外层 MATCH）
MATCH (n), (m) WHERE (n)-[:KNOWS]->(m) RETURN n, m
```

**实现方式**：EXISTS 内部模式编译为独立物理计划子树（SemiJoinPhysicalOp），通过 `CorrelatedSourcePhysicalOp` 注入外部关联变量值。`NOT EXISTS` 转换为 AntiSemiJoin。两节点关联场景下，终点变量在子计划内重命名为 `__exists_dst_<n>`，外层值通过 `__exists_saved_<n>` 透传，Expand 后用 EQ 过滤实现端到端匹配。

**当前支持**：

| 特性 | 状态 |
|------|------|
| `EXISTS { pattern }`（无内部 WHERE） | 已实现 |
| `EXISTS { pattern WHERE ... }` | 已实现 |
| `NOT EXISTS { ... }` | 已实现 |
| 多 EXISTS AND 组合 | 已实现 |
| 裸模式谓词 `WHERE (n)-[]->()` | 已实现 |
| 两节点关联 `WHERE (n)-[]->(m)` | 已实现 |
| 拒绝 RETURN/WITH 投影中的裸模式 | 已实现（`UnexpectedSyntax`） |
| 拒绝纯节点 `WHERE (n)` | 已实现（`InvalidArgumentType`） |
| 单跳 / 多跳展开 | 已实现 |
| 无向展开 `-[...]-` | 已实现 |
| EXISTS 内部属性过滤（`WHERE m.name = ...`） | 部分（属性下推有已知问题） |
| 多 EXISTS OR 组合（`EXISTS1 OR EXISTS2`） | 已实现（EXISTS 项构成的 OR 树，经 AntiSemiJoin 反演组合求值） |
| 变长模式谓词 `WHERE (n)-[:REL*]-()` | 未实现 |
| `EXISTS { MATCH ... RETURN ... }`（完整子查询） | 已实现（首个 MATCH + 可选 WITH/RETURN；更新子句在绑定期报错） |
| 嵌套 EXISTS | 已实现（简单/完整子查询嵌套均支持） |
| EXISTS 在 RETURN/CASE 等非 WHERE 上下文 | 未实现 |

**模式推导式（Pattern Comprehension）**：`[(n)-->(m) | expr]` 已实现，且支持嵌套在列表推导式中，例如 `[x IN nodes(p) | size([(x)-->(:Y) | 1])]`。含模式推导式的顶层列表推导式在绑定阶段降级为外层 `PatternComprehensionApplyOp`：右子树先 `UNWIND` 迭代列表，再对每个元素执行模式推导式，最后 `collect` 回单行列表；不改变普通列表推导式与普通模式推导式的既有执行路径。

### 多标签属性访问

```cypher
-- 便捷模式：自动搜索所有标签，单标签命中返回标量，多标签同名属性合并为列表
MATCH (n:Person) RETURN n.name

-- 转型模式：限定在指定标签内查找属性
MATCH (n:Person) RETURN n::Employee.salary

-- 返回指定标签的全部属性（map/dict 形式）
MATCH (n) RETURN n::Employee
```

### WITH — 中间投影

WITH 子句用于在查询中间进行投影、聚合、排序和过滤，类似于 RETURN 但会重置作用域（只有 WITH 列出的变量在后续子句中可见）。

```cypher
-- 简单投影 + 重命名
MATCH (n:Person) WITH n.name AS name RETURN name

-- 聚合 + 分组（非聚合列自动成为分组键）
MATCH (n:Person)-[:KNOWS]->(m) WITH n, count(m) AS cnt RETURN n.name, cnt

-- WHERE 过滤（类似 SQL HAVING，在聚合之后执行）
MATCH (n:Person)-[:KNOWS]->(m) WITH n, count(m) AS cnt WHERE cnt > 1 RETURN n.name

-- ORDER BY + LIMIT
MATCH (n:Person) WITH n.name AS name ORDER BY name DESC LIMIT 5 RETURN name

-- DISTINCT
MATCH (n:Person) WITH DISTINCT n.city AS city RETURN city
```

**支持的功能**：
- 投影 + 重命名（`expr AS alias`）
- 聚合（count/sum/avg/min/max）+ 隐式分组
- `WHERE` 过滤
- `ORDER BY` / `SKIP` / `LIMIT`
- `DISTINCT`
- 作用域重置（仅 WITH 输出列在后续可见）

**已知限制**：
- WITH 后接 MATCH 时，第二个 MATCH 创建独立扫描算子，不使用 WITH 输出作为输入（如 `WITH n.city AS city MATCH (c:City {name: city})` 中 `city` 无法传入第二个 MATCH）
- 聚合结果类型为 `ANY`，WHERE 中与具体类型（如 int64）比较可能失败（如 `WHERE count > 1`）
- WITH 后 SET 属性更新可能不生效（作用域重置后列索引变化）

### UNWIND — 列表展开

UNWIND 子句将列表表达式展开为独立行，每个元素产生一行输出。

```cypher
UNWIND [1, 2, 3] AS x
RETURN x

-- 空列表 → 0 行
UNWIND [] AS empty
RETURN empty

-- NULL → 0 行
UNWIND null AS nil
RETURN nil

-- 嵌套列表双层展开
WITH [[1,2,3],[4,5,6]] AS lol
UNWIND lol AS x
UNWIND x AS y
RETURN y

-- 重复值保留
UNWIND [1, 1, 2, 2] AS duplicate
RETURN duplicate
```

**语义**：
- 空列表 → 不产生任何行
- NULL → 不产生任何行
- 非列表值 → 运行时错误
- 可作为首子句（无输入源）或接在其他子句之后
- 原始变量在 UNWIND 后仍保持作用域（不裁剪上下文）

**限制**：
- 列表拼接 `+` 尚未支持 LIST_CONCAT 语义（`UNWIND (first + second) AS x` 会报错）
- `range()` 函数尚未注册（`UNWIND range(1, 3) AS x` 会报错）
- `RETURN *` 尚未实现变量展开（配合 UNWIND 时无法返回全部变量）

### UNION / UNION ALL — 查询合并

UNION 子句将两个以上的查询结果合并为一个结果集。

```cypher
-- UNION（去重）
RETURN 1 AS x
UNION
RETURN 2 AS x

-- UNION ALL（保留重复行）
RETURN 1 AS x
UNION ALL
RETURN 2 AS x

-- 多 UNION 链
RETURN 2 AS x
UNION
RETURN 1 AS x
UNION
RETURN 2 AS x
```

**语义**：
- `UNION` 对合并结果去重（通过 DistinctPhysicalOp 实现）
- `UNION ALL` 保留两部分的所有行，不做去重
- 同一查询中不能混合 `UNION` 和 `UNION ALL`（编译时报 `InvalidClauseComposition`）
- 所有子查询必须返回相同数量的列，且列名必须一致（编译时报 `DifferentColumnsInUnion`）
- 每个 UNION 子查询使用独立的 BindContext，变量作用域互不干扰

### RETURN — 返回

```cypher
RETURN n.name, n.age
RETURN n.name AS name, n.age AS age
RETURN *                     -- 返回所有变量（按变量名字典序）
RETURN DISTINCT n.city       -- 去重
RETURN n::Employee           -- 返回指定标签全部属性
RETURN true OR false         -- 无源 RETURN（无需 MATCH，求值常量表达式）
RETURN 1 + 2 * 3            -- 算术表达式
```

- 无 `AS` 的列名保留查询中的**原始文本**（含大小写与空白），例如 `RETURN cOuNt( * )` 的列名为 `cOuNt( * )`。
- `RETURN *` 只展开用户可见变量，按变量名**字典序**排列；作用域中没有可见变量时编译报 `NoVariablesInScope`。

### ORDER BY — 排序

```cypher
ORDER BY n.age ASC           -- 升序（默认）
ORDER BY n.age DESC          -- 降序
ORDER BY n.city, n.age DESC  -- 多键排序
```

### SKIP / LIMIT — 分页

```cypher
SKIP 10                      -- 跳过前 N 行（字面量在编译期校验）
LIMIT 20                     -- 限制返回行数（字面量在编译期校验）
SKIP $skipAmount             -- 参数在运行时求值并校验
LIMIT toInteger(ceil(1.7))   -- 无变量常量表达式在运行时求值并校验
```

- 参数与无变量常量表达式在**运行时**求值；负值/浮点参数按 TCK 语义在运行时报错。
- 表达式引用了查询变量时编译报 `NonConstantExpression`。
- `LIMIT` 只裁剪可见结果，上游写操作（CREATE/SET/DELETE 等）仍会消费全部输入行以保留副作用。

### EXPLAIN — 计划查看

```cypher
EXPLAIN MATCH (n:Person) RETURN n
```

仅构建物理算子树并格式化为文本返回，不实际执行。

---

## 二、写查询

### CREATE — 创建

```cypher
CREATE (n:Person {name: 'Alice', age: 30})
CREATE (n:Person)-[:KNOWS {since: 2020}]->(m:Person)
```

**限制**：
- 属性值仅支持字面量（字符串/整数/浮点/布尔），不支持表达式
- 仅解析第一个关系类型名
- 支持多标签 CREATE（`CREATE (n:Person:Employee)` 和 `CREATE (n:Person:Employee {age: 30, salary: 50000})`），属性按标签解析（单个命中→写入该标签，无命中→`__anon__`，多个命中→歧义报错）

### SET — 属性/标签设置

```cypher
SET n:Employee                    -- 给顶点添加标签
SET n.name = 'Bob'                -- 设置属性（便捷模式，自动查找标签）
SET n::Employee.salary = 10000    -- 设置指定标签下的属性（强模式）
SET n += {age: 30, city: 'BJ'}   -- 合并 map 属性（+= 保留已有属性）
SET n = {city: 'Beijing'}        -- 替换全部属性（= 先删除已有属性再写入）
```

**SET += / SET = 语义**：
- `+=` 合并（merge）：逐 key 写入 map 中的属性，不影响已有属性
- `=` 替换（replace）：删除所有已有属性（含 `__anon__`），再写入 map 中的属性
- 每个 map key 走便捷模式：单标签命中→写入该标签，无命中→写入 `__anon__`，多标签命中→运行时错误

### DELETE — 删除顶点/边

```cypher
MATCH (n:Label)
DELETE n                     -- 删除顶点（需无边连接）

MATCH (n:Label)
DETACH DELETE n              -- 级联删除顶点及其所有邻边

MATCH ()-[r:TYPE]->()
DELETE r                     -- 删除边
```

**语义**：
- `DELETE v` 删除顶点。若顶点仍有边连接则操作失败。
- `DETACH DELETE v` 级联删除顶点及其所有邻边（双向），无需预先删除边。
- `DELETE e` 删除边。
- DELETE 子句需要前置子句（MATCH/CREATE/WITH），不能作为首子句。
- 支持同时删除多个实体：`DELETE a, r, b`
- DELETE 后的变量引用（如表达式 `1+1`、属性访问 `n.prop`）会报编译错误。

### REMOVE — 属性/标签移除

```cypher
REMOVE n:Employee                 -- 移除顶点标签
REMOVE n.name                     -- 移除属性（便捷模式）
REMOVE r.prop                     -- 移除边属性
```

### FOREACH — 逐元素执行更新

```cypher
-- 对路径上的所有点打标
MATCH p=(start)-[*]->(finish)
FOREACH (n IN nodes(p) | SET n.marked = true)

-- 用聚合出来的列表成批建点
WITH ['E', 'F', 'G'] AS names
FOREACH (value IN names | CREATE (:Person {name: value}))

-- body 里可以套 body，也可以读写外层变量
MATCH (p:Person {id: 1})
FOREACH (x IN [1, 2, 3] | SET p.total = coalesce(p.total, 0) + x)
```

**语法**：`FOREACH (变量 IN 列表表达式 | 更新子句...)`

**body 允许的子句**：`CREATE` / `MERGE`（含 `ON CREATE` / `ON MATCH`）/ `SET`（属性、标签）/
`REMOVE`（属性、标签）/ `DELETE` / `DETACH DELETE` / 嵌套 `FOREACH`。**body 里不能有
`MATCH` / `WITH` / `RETURN`** —— 语法层面直接拒绝（neo4j 报 `Invalid use of MATCH inside
FOREACH`）；需要逐元素 MATCH 时用 `UNWIND`。

**语义**（与 neo4j 5 逐条实测对齐）：

| 维度 | 行为 |
|------|------|
| 迭代 | 列表按顺序逐元素执行 body；**空列表与 `null` 都是无操作**，不报错 |
| 非列表值 | 当作**单元素列表**：`FOREACH (x IN 1 \| ...)` 执行一次且 `x = 1`（字符串同理） |
| 基数 | **透传**：N 行输入 → N 行输出，副作用按元素发生（这正是不能用 `UNWIND` 表达的地方——UNWIND 会按元素复制行） |
| 无前置子句 | 独立出现时执行一次（隐式单行） |
| 作用域 | body 的变量上下文与外部**隔离**：迭代变量与 body 里 `CREATE` 的变量出来都不可见（`FOREACH (x IN [1] \| CREATE (:T)) RETURN x` 报 `UndefinedVariable`） |
| 遮蔽 | 迭代变量可与外层同名，body 内指向元素，外层变量出来后不受影响 |
| 相关性 | body 可读外层变量（`p.id + x`），也可写外层实体（`SET p.total = ...`）；**同一查询内后续子句能读到新值** |
| 事务 | FOREACH 属更新子句，整个语句仍是单事务；body 内错误照常抛出 |

**已知差异与限制**：

* **同查询内的标签读回**：body 里 `SET n:Label` 之后，同一语句里 `labels(n)` 读到的仍是
  body 执行前的标签集。这**不是 FOREACH 特有**：不带 FOREACH 的
  `MATCH (n) SET n:Label RETURN labels(n)` 同样如此（标签列在同语句内不刷新）；新语句查询即可看到。
* **MERGE 需要 schema 已存在**：`FOREACH (x IN [...] | MERGE (:T {v: x}))` 中若 `:T` 与其属性
  从未登记，MERGE 不会像 CREATE 那样做隐式 DDL —— 与直接写 MERGE 的限制一致。
* 目前不支持 `FOREACH` 出现在 `EXPLAIN` 之外的特殊上下文（如 `CALL {}` 子查询，子查询本身尚未支持）。

---

## 三、仅解析（执行层未实现）

| 子句/特性 | 说明 |
|-----------|------|
| `MERGE` | 条件创建（含 ON CREATE/MATCH SET） |
| `CALL` | 过程调用（已可用，见第八节） |
| `CASE WHEN THEN ELSE END` | 条件表达式 |
| `[x IN list WHERE pred \| proj]` | 列表推导 |
| `ALL/ANY/NONE/SINGLE(...)` | 量词谓词（已实现，见 WHERE 子句） |
| `EXISTS { pattern WHERE ... }` | 存在性子查询（部分支持，见下表） |
| `list[index]` / `list[from..to]` | 下标/切片 |
| `{k: v}` / `[1, 2, 3]` | Map/List 字面量求值 |

---

## 四、聚合函数

| 函数 | 说明 | 状态 |
|------|------|------|
| `count(*)` | 计数所有行 | 已实现 |
| `count(expr)` | 计数非空值 | 已实现 |
| `count(DISTINCT expr)` | 去重计数 | 已实现 |
| `sum(expr)` | 求和（int64/double） | 已实现 |
| `avg(expr)` | 平均值（返回 double） | 已实现 |
| `min(expr)` | 最小值（仅数值） | 已实现 |
| `max(expr)` | 最大值（仅数值） | 已实现 |
| `collect(expr)` | 聚合为列表（跳过 null） | 已实现 |

支持 GROUP BY：RETURN 中的非聚合表达式自动成为分组键。全局聚合（无 GROUP BY）在无输入时返回一行（count=0，其余 null）。

---

## 五、内置函数

### 标量函数

| 函数 | 说明 |
|------|------|
| `id(node)` | 返回顶点/边的全局 ID（int64_t） |
| `type(edge)` | 返回边的类型名（string） |
| `last(list)` | 返回列表最后一个元素 |
| `head(list)` | 返回列表第一个元素 |
| `reverse(list)` | 返回反转后的列表 |
| `size(list)` | 返回列表长度 |
| `range(start, end)` | 生成 [start, end] 整数列表（步长 1） |
| `range(start, end, step)` | 生成 [start, end] 整数列表（自定义步长） |
| `toInteger(x)` | 转换为整数（string/double/bool → int64） |
| `toFloat(x)` | 转换为浮点数（string/int64/bool → double） |
| `toString(x)` | 转换为字符串 |
| `labels(vertex)` | 返回顶点的标签名列表（List\<String\>） |
| `keys(vertex)` | 返回顶点的属性名列表（List\<String\>） |
| `keys(edge)` | 返回边的属性名列表（List\<String\>） |
| `nodes(path)` | 返回路径中的顶点列表 |
| `relationships(path)` | 返回路径中的边列表 |
| `length(path)` | 返回路径长度（边数） |
| `datetime(map\|string)` | 构造带时区的日期时间（string） |
| `date(map\|string)` | 构造日期（string） |
| `time(map\|string)` | 构造带时区的时间（string） |
| `localdatetime(map\|string)` | 构造本地日期时间（string） |
| `localtime(map\|string)` | 构造本地时间（string） |
| `duration(map\|string)` | 构造 ISO 8601 时间段（string） |

**限制**：时间日期构造函数返回 STRING 类型。成员访问器（`.year`、`.month` 等）、比较运算、算术运算尚未实现（Phase 2）。

---

---

## 六、参数化查询

```cypher
-- 通过 Thrift RPC 传递参数（$param_name）
MATCH (n:Person) WHERE n.name = $name RETURN n
MATCH (n:Node) WHERE n.val > $threshold RETURN n.val
```

参数通过 RPC `executeCypher(query, graph_name, parameters)` 传递，值为 Cypher 字面值字符串（如 `"42"`、`"'hello'"`、`"true"`）。参数在 Binder 阶段替换为字面值，优化器可利用参数值进行下推优化。

---

## 七、索引 DDL

```cypher
-- 创建顶点索引
CREATE INDEX idx_name FOR (n:LabelName) ON (n.propertyName)
CREATE UNIQUE INDEX idx_name FOR (n:LabelName) ON (n.propertyName)

-- 创建边索引
CREATE INDEX idx_name FOR ()-[r:EdgeType]-() ON (r.propertyName)

-- 删除索引
DROP INDEX idx_name

-- 查看索引
SHOW INDEXES
SHOW INDEX idx_name
```

`CREATE INDEX` 同步回填已有数据后设为 PUBLIC 状态。

---

## 八、Schema 查询（图结构与字段）

两条路径，语义同一份元数据，用途不同：**`CALL` 过程**适合带 `WHERE`/`YIELD` 的自定义取数，
**`DESCRIBE`** 是单目标、一行搞定的便捷写法。

### 8.1 DESCRIBE 家族

`DESCRIBE`（别名 `DESC`）在 Cypher 解析之前被 `DatabaseDdlParser` 拦截，**按当前选中的图**取 schema，
不经过算子流水线，因此不支持 `YIELD ... WHERE`、不支持 `EXPLAIN`。

```cypher
-- 图里有哪些 label / relation 类型
DESCRIBE LABELS
DESCRIBE RELATIONSHIPS

-- 某个 label / relation 有哪些字段
DESCRIBE LABEL Person
DESCRIBE RELATIONSHIP KNOWS        -- 别名：DESCRIBE REL KNOWS

-- 名字含空格时用反引号
DESCRIBE LABEL `My Label`
```

命名规则是**复数=列出全部、单数=按名查字段**：`LABELS` / `LABEL x`、`RELATIONSHIPS` /
`RELATIONSHIP x` 两组同构，记一条规则即可。`DESC` 是整个家族的别名；`REL` 只是
`RELATIONSHIP` 的短别名（不存在 `RELS`）。这里没有保留字表——目标名可以是任何词，
所以 `DESCRIBE RELATIONSHIP TYPES` 会被当作"名为 `TYPES` 的关系类型"，未登记时返回 0 行。

输出列：

| 语句 | 列 |
|------|-----|
| `DESCRIBE LABELS` | `name`, `anonymous` |
| `DESCRIBE RELATIONSHIPS` | `relationshipType` |
| `DESCRIBE LABEL x` | `label`, `propertyName`, `propertyType` |
| `DESCRIBE RELATIONSHIP x` | `relType`, `propertyName`, `propertyType` |

* `propertyType` 是**单数、纯字符串**：一个声明字段只有一个类型（`PropertyDef.type`）。
  类型名：`BOOLEAN` / `INTEGER` / `FLOAT` / `STRING` / `*_ARRAY` / `DATE_TIME` /
  `TIME` / `DURATION` / `BYTE_ARRAY` / `ANY`。
  ⚠️ 过程路径（`db.schema.nodeTypeProperties()` / `relTypeProperties()`）的列名是
  **复数 `propertyTypes` 且为列表**——那是 neo4j 的形状，保留不动；DESCRIBE 是我们自创的
  语法面，没有该约束，因此取更好用的单值形式。
* 字段行按 `propertyName` 升序；`DESCRIBE LABELS` 的行按 `name` 升序 —— 输出可复现。
* **查不到不报错**：未知 label/relation 返回 0 行（列形状仍在）。
* 名字区分大小写（关键字不区分）。

### 8.2 匿名标签（`__anon__`）

无标签的节点把属性挂在内部标签 `__anon__` 上，所以它和普通 label 一样有 schema。本引擎
**把它当作普通标签暴露**出来——`DESCRIBE LABELS`、`CALL db.labels()`、
`CALL db.schema.nodeTypeProperties()` 都会报告它，`DESCRIBE LABEL __anon__` 可以查它的字段。
需要区分时看 `DESCRIBE LABELS` 的 `anonymous` 列。

> ⚠️ 与 neo4j 的有意差异：neo4j 的 `db.labels()` / `db.schema.nodeTypeProperties()` **不返回**
> 匿名 label（neo4j 也不存在这个内部标签）。我们返回，因为"无标签节点的字段"同样是需要发现的
> schema；隐藏它会让这类字段在 Cypher 侧无从查起。

### 8.3 CALL 过程（等价路径）

```cypher
CALL db.labels()                     RETURN label              -- 所有 label（含 __anon__）
CALL db.relationshipTypes()          RETURN relationshipType   -- 所有 relation 类型
CALL db.propertyKeys()               RETURN propertyKey        -- 所有属性名
CALL db.schema.nodeTypeProperties()  RETURN nodeLabels, propertyName, propertyTypes, mandatory
CALL db.schema.relTypeProperties()   RETURN relType, propertyName, propertyTypes
CALL db.schema.visualization()       RETURN nodes, relationships
```

过程参数与 `YIELD` 已支持（`CALL proc(args) YIELD a, b AS c RETURN ...`），但
**`YIELD ... WHERE ...` 绑定器尚未支持**，过滤请写在 `RETURN` 之后或用 `WITH`。
完整清单见 `CALL dbms.procedures()` / `SHOW PROCEDURES`。

### 8.4 已声明的类型 vs 实际写入

`propertyType` 来自 label 的**声明 schema**，而声明由写入路径决定：

* `CREATE (:Person {name: 'Alice'})` 首次看到属性时登记为 **`ANY`**（值本身按实际类型存储）；
* `SET n.name = 'Alice'` **只写数据、不登记 schema**，不会让字段出现在 `DESCRIBE` 结果里；
* 通过 `createLabel` / `createEdgeLabel` 声明了类型（如 `name: STRING`）时，`DESCRIBE` 报声明的类型。

⚠️ **写语句返回即代表已生效**（客户端契约）：服务端把写语句的副作用放在结果流的生成器里，
调用方**必须把流读完**才能认为语句执行完毕。历史上 `eugraph-shell` 在"结果无列"时直接打印
`OK` 而不订阅流，于是裸 `CREATE` 会在 schema 登记尚未落定时就报告成功，紧随其后的
`DESCRIBE` 可能看不到刚登记的字段。已修：shell 现在无论有无列都排空结果流。

也就是说 `DESCRIBE` 反映的是**元数据里登记了什么**，不保证等于数据里实际出现的类型。
（`mandatory` 列**暂未提供**：`PropertyDef.required` 目前没有任何地方置为 `true`，写入路径也不校验，
加了会恒为 `false` 而误导。）
