# 顶点属性归属：强空间与弱空间

> 本文是「属性写到哪」的**唯一权威规则**。冲突时以本文为准。
> 相关背景见 [mixed-mode-design.md](../refactor/mixed-mode-design.md)、
> [multi-label-design.md](multi-label-design.md)、[unlabeled-node-property-design.md](unlabeled-node-property-design.md)。

## 1. 一句话规则

**标签的字段只由 DDL 定义；DML 只写数据，不定义字段。**

DML（`CREATE` / `SET` / `MERGE`）遇到标签未声明的属性时，**一律写入弱空间**（`__anon__`），
不得给标签补列。自动创建标签只创建「名字」（一个空标签），不创建字段。

原因：如果 DML 能补列，强模式的 `::` 校验就永远为真（列总是存在），强/弱两种模式也就没有边界了；
同时列的落点会依赖语句形态与写入历史（`CREATE` 补 `ANY`、`SET` 补首写值类型、`MERGE` 补 `ANY`），
不可预测、不可逆（`prop_id` 永不复用）、无法索引。

## 2. 两个空间

| | 强空间 | 弱空间 |
|---|---|---|
| 载体 | 标签的声明列（`LabelDef.properties`） | 顶点自有属性（隐藏标签 `__anon__`） |
| 定义者 | DDL（loader schema / `CREATE LABEL`） | 首次写入（`getOrCreateAnonPropId`） |
| 类型 | 声明类型 | 无类型（`ANY`） |
| 索引 | 可建 | 不可建 |
| 访问 | `n::L.p`（严格） | 只能 `n.p`（便捷读/写） |
| 出现位置 | `DESCRIBE LABEL L` | `DESCRIBE LABEL __anon__` |

一个顶点 = 「若干标签的记录」+「一个顶点自有属性袋」。弱空间不是标签，**永远是兜底，不是对等候选**。

## 3. 写入解析

设 `p` 为待写属性，`L(n)` 为顶点 n **实际持有**的标签集合（不含 `__anon__`）：

| 语句 | `L(n)` 中恰好一处声明 p | 多处声明 p | 无人声明 p |
|---|---|---|---|
| `CREATE (n:L {p: v})` | 写该标签的列 | **报错**（`AmbiguousProperty`） | 写弱空间；L 不存在则建空标签 |
| `SET n.p = v` | 写该标签的列 | **报错**（要求 `n::Label.p`） | 写弱空间（必要时给顶点挂上 `__anon__`） |
| `SET n::L.p = v` | 写 L 的列 | — | **binder 报错**（提示 DDL 或 `n.p`） |
| `MERGE (n:L {p: v})` | 按该列匹配/写入 | **报错** | 按弱空间匹配/写入；L 不存在则建空标签 |
| `ON CREATE/ON MATCH SET n.p = v` | 写该标签的列 | **报错** | 写弱空间 |

补充：

- 「声明」只算**本节点标签**的 schema，不是全局扫描。`Person` 声明了 `a`，不影响只有 `Role` 的节点。
- `CREATE` 的自动建标签只建空标签：`CREATE (n:Test {a: 1})` ⇒ `Test` 存在且**无列**，`a` 在弱空间。
- 多标签 `CREATE` 中某属性被多个指定标签声明时**不再 fan-out**（旧行为会把同一个值写进每个标签，
  读回 `[1, 1]`）；现在报错，需要按标签分别写。
- 每个写路径都必须保证：属性落到弱空间时，顶点挂上 `__anon__` 标签。ProjectionExtract 只对
  带 `__anon__` 的顶点预取弱空间属性（`projection_extract_physical_op.cpp`），否则后续读会是 null。

## 4. 读取解析

- `n.p`：先看声明列。
  - 恰好一处声明 → 该列的值；该列为 null → 回退到弱空间同名属性（兼容历史数据）。
  - 多处声明 → 各自非空值合并为列表（仅显式按标签写入才会多值）。
  - 无人声明 → 弱空间；弱空间也没有 → null。
  - **弱空间的值永不与声明列的值合并成列表**：声明列有值即胜出。
- `n::L.p`：只看 L 的声明列。
  - L 不存在 或 L 没声明 p → **binder 报错**。
  - 顶点没有 L → 返回 null（转型读类似 LEFT JOIN）。
- `properties(n)` / `keys(n)`：两个空间都包含（对外可见的属性集合不变）。

## 5. `REMOVE n:L`

- 删除该顶点在 L 下的**标签记录 + L 的表里的全部属性**；**不迁移**到弱空间。
- 弱空间属性不受影响。
- 顶点因此没有任何标签时，顶点本身保留：`MATCH (n)` 仍能看到它，`labels(n) = []`。
- 多标签顶点只移除 L，其余标签的属性不受影响。

## 6. 强模式写 `SET n::L.p`

三道校验：

1. 标签 L 不存在 → binder 报错。
2. L 没有声明 p → binder 报错（提示 DDL 或 `n.p`）。
3. 顶点 n 没有 L → **运行时报错**（旧实现会写出一条孤儿属性：`MATCH (n:L)` 看不到、`properties(n)` 看不到，
   只有 `n::L.p` 能读出来）。要写请先 `SET n:L`。

## 7. 边界与例外

- **边是纯强模式**：没有弱空间。`CREATE ()-[:NEW {p: 1}]->()` 会创建类型 `NEW` 并把它用到的属性作为
  该类型的列（bootstrap，等价于「用一条语句声明类型」）。类型已存在但属性未声明时仍会扩列，
  这是本次尚未统一的遗留行为，单独跟踪。
- `ALTER LABEL L ADD PROPERTY p T`（把弱空间的键提升为列）**暂不实现**。需要新列时先建新标签，
  或走 loader schema 的增量 merge。在它实现前，弱空间的键不可索引、不可 `::` 访问。
- 自动建标签不记录来源（`origin: DDL|DML`）暂不实现。

## 8. 与旧行为的差异（兼容性）

| 场景 | 旧行为 | 新行为 |
|---|---|---|
| `CREATE (n:Test {a:1})`，Test 不存在 | Test 得到 `a ANY` 列，a 存 Test | Test 为**空**标签，a 存弱空间 |
| `CREATE (n:Person {nickname:'x'})`，Person 已存在 | Person 追加 `nickname ANY` 列 | 不动 Person schema，nickname 存弱空间 |
| `SET n.p = v` 无命中 | 追加到第一个用户标签（值类型） | 写弱空间 |
| `SET n.p = v` 多命中 | 静默 no-op | 报错 |
| 多标签 `CREATE` 多声明 | fan-out 到每个标签，读回 `[1,1]` | 报错 |
| `SET n::L.p`，顶点无 L | 写孤儿行 | 报错 |
| `REMOVE n:L` | 把 L 的属性迁到弱空间 | 删除 L 的属性 |
| `n.p` 同键既有声明列又有弱值 | 合并成列表 | 声明列优先；列为 null 才回退弱空间 |

历史数据：旧版本经 DML 扩出来的列保留（仍是声明列），新写入不再扩列；旧数据不需要迁移。
