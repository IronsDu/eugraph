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

**实现方式**：EXISTS 内部模式编译为独立物理计划子树（SemiJoinPhysicalOp），通过 `CorrelatedSourcePhysicalOp` 注入外部关联变量值。`NOT EXISTS` 转换为 AntiSemiJoin。

**当前支持**：

| 特性 | 状态 |
|------|------|
| `EXISTS { pattern }`（无内部 WHERE） | 已实现 |
| `EXISTS { pattern WHERE ... }` | 已实现 |
| `NOT EXISTS { ... }` | 已实现 |
| 多 EXISTS AND 组合 | 已实现 |
| 单跳 / 多跳展开 | 已实现 |
| 无向展开 `-[...]-` | 已实现 |
| EXISTS 内部属性过滤（`WHERE m.name = ...`） | 部分（属性下推有已知问题） |
| `EXISTS { MATCH ... RETURN ... }`（完整子查询） | 未实现 |
| 嵌套 EXISTS | 未实现 |
| EXISTS 在 RETURN/CASE 等非 WHERE 上下文 | 未实现 |

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

### RETURN — 返回

```cypher
RETURN n.name, n.age
RETURN n.name AS name, n.age AS age
RETURN *                     -- 返回所有变量
RETURN DISTINCT n.city       -- 去重
RETURN n::Employee           -- 返回指定标签全部属性
RETURN true OR false         -- 无源 RETURN（无需 MATCH，求值常量表达式）
RETURN 1 + 2 * 3            -- 算术表达式
```

### ORDER BY — 排序

```cypher
ORDER BY n.age ASC           -- 升序（默认）
ORDER BY n.age DESC          -- 降序
ORDER BY n.city, n.age DESC  -- 多键排序
```

### SKIP / LIMIT — 分页

```cypher
SKIP 10                      -- 跳过前 N 行（仅支持字面量整数）
LIMIT 20                     -- 限制返回行数（仅支持字面量整数）
```

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
SET n::Employee.salary = 10000    -- 设置指定标签下的属性
```

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
```

---

## 三、仅解析（执行层未实现）

| 子句/特性 | 说明 |
|-----------|------|
| `MERGE` | 条件创建（含 ON CREATE/MATCH SET） |
| `CALL` | 过程调用/子查询 |
| `UNION` / `UNION ALL` | 查询合并 |
| `OPTIONAL MATCH` | 可选匹配 |
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
