# 强/弱混合属性模式

> **权威文档**：属性归属、读写、索引、多标签差异都以本文为准。
> 通用多标签语义（一个节点可有多个标签、`labels(n)`、标签集合操作）遵循 Neo4j/Cypher，
> 本文不重复介绍，只描述 EuGraph 特有的「属性按标签分列 + 弱空间」部分。
> 引擎实现细节（MERGE 执行算法、计划形状）见
> [merge-clause-design.md](../query/engine/merge-clause-design.md)、[docs/README.md](../README.md)。

## 1. 为什么是混合模式

| 系统 | 问题 |
|---|---|
| **NebulaGraph** | 强 schema，属性严格绑定标签；跨标签读同一业务字段非常繁琐，必须记住字段属于哪个标签 |
| **Neo4j** | 无 schema，属性是顶点上的扁平字典；探索方便，但没有列类型/强索引保障，拼写或类型错误容易静默 |

EuGraph 取中间路线：

- **强空间**：标签拥有**声明列**（DDL 定义）。静态类型、可建强索引、`n::L.p` 编译期校验。
- **弱空间**：未声明的键、以及无标签节点的属性，写入顶点自有属性袋 `__anon__`。动态、无类型、可建弱索引。
- `n.p` 是便捷读写：跨两个空间按「家」解析；`n::L.p` 是强访问：只看指定标签。
- 同一顶点可以同时有强/弱属性；不做数据迁移。

三条边界：

1. **DML 只写数据，不定义字段**；标签字段只由 DDL 定义。
2. **自动创建标签只建空标签**（`CREATE (n:Test {a:1})` 不会给 Test 加 `a` 列）。
3. **非目标**：per-vertex schema、弱键提升为列的 `ALTER`（#316）、边的弱模式（#317）。

## 2. 多标签：只讲差异

通用语义与 Neo4j 一致，不在此重复：

- 一个节点可以有多个标签；`labels(n)` 返回标签名列表；
- `SET n:L` 增加标签、`REMOVE n:L` 删除标签；标签可以没有属性（纯标记/分类）；
- `MATCH (n:A:B)` 要求同时具有 A、B。

> 注：引擎目前不把 `CREATE LABEL` 作为 Cypher 语句暴露；它由 loader schema / Thrift
> `createLabel`（shell 的 `:create-label`）提供。TCK 用 `Given the schema:` +
> `CREATE LABEL Name (p TYPE, ...)` 文本封装同一条路径。

参考：[Neo4j Cypher Manual — Patterns](https://neo4j.com/docs/cypher-manual/current/patterns/)、
[Scalar functions（`properties()` / `keys()`）](https://neo4j.com/docs/cypher-manual/current/functions/scalar/)，
以及本仓库 `third_party/openCypher/tck/features/` 下的 `match`/`set`/`remove` 用例。

EuGraph 的差异只有一个：**属性按标签分列存储**。`VertexValue.properties` 是
`map<LabelId, Properties>`，每个标签的 `prop_id` 独立编号。因此同一个键可能在多个标签、
或标签 + `__anon__` 下各有一份，这才需要下面的「家」和 `::`：

```cypher
MATCH (n:Person) RETURN n::Employee.salary   -- 只看 Employee 的列；没有 Employee 标签 → null
MATCH (n) RETURN keys(n::Employee)           -- Employee 标签的字段名
MATCH (n) RETURN properties(n::Employee)     -- Employee 标签的属性 map（没有该标签 → 空 map）
```

`n::L` 本身返回一个**只保留 L 的 properties 的顶点视图**（id/labels 保留，其他标签的
properties 被清空；顶点没有 L 时 properties 为空，不是 null）。要纯 map 时用 `properties(n::L)`。

多标签 `CREATE` 的属性归属：在**指定标签**中恰好一个声明该键 → 写该列；多个声明 →
binder 报 `AmbiguousProperty`；无人声明 → 写 `__anon__`。

`REMOVE n:L`：删除该顶点在 L 下的标签记录 + L 的属性，**不迁移**到弱空间；顶点可以因此变成裸顶点
（`MATCH (n)` 仍可见，`labels(n) = []`）。

## 3. 两个空间与「家」

| | 强空间 | 弱空间 |
|---|---|---|
| 载体 | 标签的声明列（`LabelDef.properties`） | 顶点自有属性（隐藏标签 `__anon__`） |
| 定义者 | DDL（loader schema / `CREATE LABEL`） | 首次写入（`getOrCreateAnonPropId`），键存在即注册 |
| 类型 | 声明类型 | 无类型（`ANY`） |
| 索引 | 强索引（只看 L 的列） | 弱索引（按「家」解析） |
| 访问 | `n::L.p`（严格） | `n.p`（便捷读/写） |
| 出现位置 | `DESCRIBE LABEL L` | `DESCRIBE LABEL __anon__` |

**家**（按顶点实际状态）：

- 顶点实际持有的、**声明了**该键的每个标签，各算一个家（列存在即算，值可以是 NULL）；
- `__anon__` 里该键**有值**，算一个家（只看该顶点是否有值，不看全局键注册表）。

**一个键在一个顶点上只能有一个家；多个家就是歧义，歧义一律报错。** 读、写目标解析、索引能否使用、
索引存在时写入能否成功，全部用这一个判断。强访问 `n::L.p` 显式指定家，不受歧义影响。

## 4. 读

`n.p`（弱视图）按家数解析：

| 家数 | 结果 |
|---|---|
| 0 | `null` |
| 1（标签） | 该列的值（列 NULL 就是 `null`，**不回退 anon**） |
| 1（`__anon__`） | 该弱值 |
| ≥2 | **报错 `AmbiguousProperty`**，列出冲突的家，提示用 `n::Label.p` |

- 歧义按**家数**判断，不看值是否相等：两个标签都声明 p、值相同（甚至都为 NULL）也是歧义。
- `n::L.p`：只看 L 的声明列；L 不存在/未声明 → binder 报错；顶点没有 L → `null`。
- `n::L`：返回只保留 L properties 的顶点视图（见 §2）；`keys(n::L)` / `properties(n::L)` 可用于取字段名/属性 map。
- `properties(n)`：返回 Cypher **MAP**，按同一个家规则摊平——某个键 ≥2 个家 → 报错；
  1 个家有值 → 一个条目；1 个家但值为 NULL / 0 个家 → 不出现。
- `keys(n)`：返回 **LIST<STRING>**，是有值属性名的**去重并集**；名字集合本身无歧义，不因多个家报错。
- ORDER BY、聚合、`n[propName]` 与 `n.p` 使用同一判断；`n[propName]` 在绑定时没有静态来源，
  本次查询不走索引（键注册后的后续语句可以）。

## 5. 写

设 `H(p)` 为顶点上键 `p` 的家数：

| `H(p)` | `SET n.p = v` | 说明 |
|---|---|---|
| 0 | 写 `__anon__`（新建一个家） | 弱写 |
| 1（标签） | 写该标签的列 | |
| 1（`__anon__`） | 写 `__anon__` | |
| ≥2 | **报错 `AmbiguousProperty`** | 写目标本身无法确定 |

| 语句 | 行为 |
|---|---|
| `CREATE (n:L {p: v})` | 指定标签中恰好一个声明 p → 写该列；多个声明 → binder 报错；无人声明 → 写 `__anon__`；L 不存在 → 建空标签 |
| `MERGE (n:L {p: v})` / `ON CREATE/ON MATCH SET n.p = v` | 同上（按 `H(p)` 解析） |
| `SET n::L.p = v` | 只写 L 的列；L 不存在 → binder 报错；L 未声明 p / 顶点没有 L → 运行时报错。允许在歧义顶点上显式写，但不会自动消除已有的 anon 副本 |
| `SET n:L` | 允许（即使因此制造歧义）；**但若该键存在弱索引，索引维护失败 → 写失败**（见 §7） |
| `SET n.p = null` / `REMOVE n.p` | 删除**所有家**（声明列 + `__anon__`）；这是消除歧义的手段 |
| `SET n = {...}` / `SET n += {...}` | 每个 key 按 `H(p)` 解析；`=` 先删除旧值，再按家解析 |

补充：

- 「声明」只算**本节点实际标签**的 schema，不是全局扫描。
- 多标签 `CREATE` 中某属性被多个指定标签声明时**不再 fan-out**，直接报错。
- 弱写落到 `__anon__` 时，顶点会挂上隐藏 `__anon__` 标签（ProjectionExtract 只对带该标签的顶点预取弱属性）。
- MERGE 的 `SET m = n`（实体源）与 `properties(n)` 共用摊平规则：歧义报错。

## 6. 强模式写 `SET n::L.p`

三道校验：

1. 标签 L 不存在 → binder 报错。
2. L 没有声明 p → **运行时报错**（`SET` 物理算子检查声明）。
3. 顶点没有 L → **运行时报错**。

强写是显式指定家，允许在歧义顶点上执行，但不会消除已有的 anon 副本；要消除请用 `REMOVE n.p`。

## 7. 索引

索引的作用域由建索引语句的访问器写法决定，并与查询的访问方式一一对应：

| DDL | 覆盖范围 | 建索引前置条件 | 能服务的查询 |
|---|---|---|---|
| `CREATE INDEX ... FOR (n:L) ON (n::L.p)` | **只** L 的声明列 | L 必须已声明 p（创建索引/建表之前校验） | 强访问 `n::L.p`；**以及**弱访问 `n.p`，但仅当 p 全局只被一个标签声明、且弱空间从未注册过 p（不可能出现歧义行） |
| `CREATE INDEX ... FOR (n:L) ON (n.p)` | 弱视图：按「家」解析的值 | 无（键尚不存在也允许：弱列在首次写入时自动创建） | 只能服务弱访问 `n.p` |

- **弱索引的取值必须与弱读逐行一致**：有声明标签取声明列（唯一家），否则取 `__anon__`。
  回填遇到歧义顶点 → **建索引失败**；索引已存在时，任何会让该键出现歧义的写
  （`SET n:L`、MERGE `SET n:L`、`SET n.p` 等）→ **写失败**（弱索引无法表示歧义）。
- **强索引只物化 L 的列**：不索引 anon/其他标签的值，因此 `SET n:L` 制造歧义时**不会**因为强索引而失败。
- 查询侧模式匹配（`BoundPropertyRef.strong_mode` + 声明的标签数）：
  - 强查询 `n::L.p` 只能使用 L 上的强索引；**永不使用弱索引**（弱索引含 anon/其他标签的值，会误报）。
  - 弱查询 `n.p` 可使用弱索引；只有满足「单标签声明 + 弱空间无该键」时才可使用强索引。
- 键不存在时允许创建弱索引：首次弱写分配 `prop_id` 后，写路径补索引条目。

## 8. 边界与已知问题

- **边是纯强模式**：没有弱空间。`CREATE ()-[:NEW {p: 1}]->()` 会创建类型 `NEW` 并把它用到的属性作为该类型的列
  （bootstrap）。类型已存在但属性未声明时仍会扩列，这是尚未统一的遗留行为，单独跟踪（#317）。
- `ALTER LABEL L ADD PROPERTY p T`（把弱键提升为列）**暂不实现**（#316）。需要新列时先建新标签，
  或走 loader schema 的增量 merge。
- 自动建标签不记录来源（`origin: DDL|DML`）暂不实现。
- `RETURN n` 的 Bolt/Thrift 整实体序列化仍是 last-wins（已知遗留）；`properties(n)` 已经按家报错。
- 弱键存在于弱空间注册表时，弱读走整顶点求值（较慢）；强索引不再服务该键的弱查询。
- 歧义数据修复路径：`REMOVE n.p`（删除所有家）→ 再用 `SET n::L.p` 或弱写建立唯一的家。
  若该键有弱索引，先 `DROP INDEX` 再清理。

## 9. 与旧行为的差异（兼容性）

| 场景 | 旧行为 | 新行为 |
|---|---|---|
| `CREATE (n:Test {a:1})`，Test 不存在 | Test 得到 `a ANY` 列，a 存 Test | Test 为**空**标签，a 存弱空间 |
| `CREATE (n:Person {nickname:'x'})`，Person 已存在 | Person 追加 `nickname ANY` 列 | 不动 Person schema，nickname 存弱空间 |
| `SET n.p = v` 无命中 | 追加到第一个用户标签（值类型） | 写弱空间 |
| `SET n.p = v` 多命中 | 静默 no-op | 报错（歧义） |
| 多标签 `CREATE` 多声明 | fan-out 到每个标签，读回 `[1,1]` | 报错 |
| 多标签同键、值相等/一边 NULL | 合并为列表 / 返回标量 | **报错**（多个家） |
| anon 有值 + 标签声明同键 | 声明列优先或回退 anon | **报错**（多个家） |
| `SET n::L.p`，顶点无 L | 写孤儿行 | 报错 |
| `REMOVE n:L` | 把 L 的属性迁到弱空间 | 删除 L 的属性 |
| 弱空间建索引 | 不支持 | 弱索引可建；歧义顶点回填失败；索引存在时会阻止制造歧义的写 |
| 强索引服务弱查询 | 不支持 | 单标签声明且弱空间无该键时可用 |
| 弱查询使用强索引 | 可能误用 | 不允许（除非无歧义可能） |
| `properties(n)` 多标签同键 | 重复键 / last-wins | 报 `AmbiguousProperty`（值相等、NULL 列 + anon 值也算） |
| `keys(n)` 多标签同键 | 重复名字 | 去重并集，不报错 |
| 索引 DDL 未声明属性 | 静默成功 | 强访问器报错；弱访问器允许（键可尚不存在） |

历史数据：旧版本经 DML 扩出来的列保留（仍是声明列），新写入不再扩列；旧数据如果同时存在 anon 副本，
弱读会报歧义，按 §8 的路径清理。

## 10. 实现入口

| 层 | 文件 |
|---|---|
| 家解析 / 弱空间读取 | `src/common/types/property_home.hpp`、`src/query/evaluator/expr/eval_property.cpp` |
| 写入归属 | `bind_mutation.cpp`、`set_physical_op.cpp`、`create_node_physical_op.cpp`、`merge_physical_op.cpp` |
| 强访问 | `bind_expression.cpp`、`bound_property_ref.hpp`（`strong_mode`） |
| 投影下推 / 索引选择 | `projection_extraction.cpp`、`projection_extract_physical_op.cpp`、`physical_planner.cpp` |
| 索引维护/回填 | `vertex_index_maintenance.hpp`、`query_executor.cpp` |
| `properties()` / `keys()` | `src/query/function/scalar/graph_functions.hpp` |
| 测试 | `tests/test_query_executor.cpp`、`tests/tck/features-eugraph/property-ownership/` |

## 11. 参考

- [Neo4j Cypher Manual — Patterns](https://neo4j.com/docs/cypher-manual/current/patterns/)（多标签语法）
- [Neo4j Cypher Manual — Scalar functions](https://neo4j.com/docs/cypher-manual/current/functions/scalar/)（`properties()` / `keys()`）
- openCypher TCK：`third_party/openCypher/tck/features/clauses/{match,set,remove,merge,with-*}`。
- [MERGE 执行设计](../query/engine/merge-clause-design.md)（引擎内部，不重复语义）。
- [TCK 指南](../tests/tck-guide.md)。
