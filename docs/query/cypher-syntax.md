# Cypher 语法参考

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

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

**限制**：
- 仅支持单个 MATCH 子句（多 MATCH 不支持）
- 仅支持单个 pattern part（逗号分隔的多个模式不支持）
- OPTIONAL MATCH 仅解析，执行按普通 MATCH 处理
- 变长路径（`*min..max`）仅解析，Expand 算子不使用

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
| 字符串 | `STARTS WITH`, `ENDS WITH`, `CONTAINS` | 仅解析 |
| 列表 | `IN` | 仅解析 |
| 逻辑 | `XOR` | 仅解析 |

**索引优化**：当 Filter 在 LabelScan 之上且谓词为 `n.prop = literal` 模式，且该属性存在 PUBLIC 索引时，自动使用 IndexScan（还支持 `>`, `>=`, `<`, `<=` 的范围扫描）。

### RETURN — 返回

```cypher
RETURN n.name, n.age
RETURN n.name AS name, n.age AS age
RETURN *                     -- 返回所有变量
RETURN DISTINCT n.city       -- 去重
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
- CREATE 不能跟在 MATCH 等子句之后（仅支持独立 CREATE）

---

## 三、仅解析（执行层未实现）

| 子句/特性 | 说明 |
|-----------|------|
| `DELETE` / `DETACH DELETE` | 删除顶点/边 |
| `SET` | 属性赋值/标签设置（4 种模式） |
| `REMOVE` | 属性/标签移除 |
| `MERGE` | 条件创建（含 ON CREATE/MATCH SET） |
| `UNWIND` | 列表展开为行 |
| `WITH` | 多部查询断点 |
| `CALL` | 过程调用/子查询 |
| `UNION` / `UNION ALL` | 查询合并 |
| `OPTIONAL MATCH` | 可选匹配 |
| 变长路径 `*min..max` | 多跳展开 |
| `CASE WHEN THEN ELSE END` | 条件表达式 |
| `$param` | 参数化查询 |
| `[x IN list WHERE pred \| proj]` | 列表推导 |
| `ALL/ANY/NONE/SINGLE(...)` | 量词谓词 |
| `EXISTS { pattern }` | 存在性子查询 |
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

支持 GROUP BY：RETURN 中的非聚合表达式自动成为分组键。全局聚合（无 GROUP BY）在无输入时返回一行（count=0，其余 null）。

---

## 五、内置函数

| 函数 | 说明 |
|------|------|
| `id(node)` | 返回顶点/边的全局 ID（int64_t） |

---

## 六、索引 DDL

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
