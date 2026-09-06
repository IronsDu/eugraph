# Loader 多标签与 Neo4j Import 风格表头设计

> 状态：设计草案（待评审）
> 分支：`feature/loader-multi-label-import`

## 1. 背景与动机

当前 `eugraph-loader` 的 CSV 约定是：

- 点文件：`{label}_0_0.csv`，一个文件只能产生一个标签；
- 边文件：`{srcLabel}_{edgeType}_{dstLabel}_0_0.csv`，且按 `_` 切分文件名，导致 `edgeType` 不能包含下划线（`HAS_CREATOR` 无法表达）；
- 表头：第一行直接作为属性名，首列隐式为点 ID，边文件前两列隐式为 src/dst ID；
- 类型推断只支持 `INT64` / `STRING`。

这导致导入 LDBC SNB 数据时，无法表达：

1. `Comment` / `Post` 同时属于 `Message` 这类多标签节点；
2. `Place` 按 `type` 列拆成 `City` / `Country` / `Continent` 这类“一行一标签”的节点；
3. `HAS_CREATOR` 这类带下划线的关系类型；
4. `speaks:STRING[]` 这类数组属性和显式类型声明。

此前 benchmark 只能通过“预先转换 CSV 目录 + 改写查询语句”绕过，不是长久方案。

## 2. 目标 / 非目标

### 2.1 目标

1. 让 loader 能导入 **多标签节点**，包括：
   - 整个文件所有节点共享多个标签（如 `Comment+Message`）；
   - 每行通过表头 `:LABEL` 列动态指定额外标签（如 `Place` 行按 `type` 得到 `City` / `Country` / `Continent`）。
2. 让 loader 能表达 **任意关系类型名**，包括带下划线的 `HAS_CREATOR`、`IS_LOCATED_IN` 等。
3. 支持 **Neo4j import 风格表头**：`:ID`、`:START_ID(Group)`、`:END_ID(Group)`、`:LABEL`、带类型后缀的属性列（`name:STRING`、`creationDate:LONG`、`speaks:STRING[]`）。
4. 保持对现有目录格式的向后兼容，现有导入目录无需改动即可继续工作。
5. 使 LDBC SNB 数据可以按 LDBC/Neo4j 相同的 schema 导入，**无需改写查询语句**。

### 2.2 非目标（本期不做）

- 不做 CSV 字段内转义/引号的完整 RFC4180 兼容；沿用当前 `|` 分隔和行式解析，仅对数组分隔符做约定。
- 不做 `LOAD CSV` Cypher 子句。
- 不做离线导入（loader 仍通过 RPC 连接运行中的 server，与 Neo4j `neo4j-admin import` 的离线模式不同）。

## 3. 使用方式与总体原则

### 3.1 CLI 参数（主模式，参考 neo4j-admin import）

推荐用户显式声明 schema，而不是依赖文件名编码。与 `neo4j-admin import` 对齐：

```bash
eugraph-loader --host 127.0.0.1 --port 9090 \
  --data-dir /path/to/neo4j-converted \
  --delimiter '|' \
  --nodes=Place=static/place_0_0.csv \
  --nodes=Organisation=static/organisation_0_0.csv \
  --nodes=TagClass=static/tagclass_0_0.csv \
  --nodes=Tag=static/tag_0_0.csv \
  --nodes=Comment:Message=dynamic/comment_0_0.csv \
  --nodes=Forum=dynamic/forum_0_0.csv \
  --nodes=Person=dynamic/person_0_0.csv \
  --nodes=Post:Message=dynamic/post_0_0.csv \
  --relationships=IS_PART_OF=static/place_isPartOf_place_0_0.csv \
  --relationships=IS_SUBCLASS_OF=static/tagclass_isSubclassOf_tagclass_0_0.csv \
  --relationships=IS_LOCATED_IN=static/organisation_isLocatedIn_place_0_0.csv \
  --relationships=HAS_TYPE=static/tag_hasType_tagclass_0_0.csv \
  --relationships=HAS_CREATOR=dynamic/comment_hasCreator_person_0_0.csv \
  --relationships=IS_LOCATED_IN=dynamic/comment_isLocatedIn_place_0_0.csv \
  --relationships=REPLY_OF=dynamic/comment_replyOf_comment_0_0.csv \
  --relationships=REPLY_OF=dynamic/comment_replyOf_post_0_0.csv \
  --relationships=CONTAINER_OF=dynamic/forum_containerOf_post_0_0.csv \
  --relationships=HAS_MEMBER=dynamic/forum_hasMember_person_0_0.csv \
  --relationships=HAS_MODERATOR=dynamic/forum_hasModerator_person_0_0.csv \
  --relationships=HAS_TAG=dynamic/forum_hasTag_tag_0_0.csv \
  --relationships=HAS_INTEREST=dynamic/person_hasInterest_tag_0_0.csv \
  --relationships=IS_LOCATED_IN=dynamic/person_isLocatedIn_place_0_0.csv \
  --relationships=KNOWS=dynamic/person_knows_person_0_0.csv \
  --relationships=LIKES=dynamic/person_likes_comment_0_0.csv \
  --relationships=LIKES=dynamic/person_likes_post_0_0.csv \
  --relationships=HAS_CREATOR=dynamic/post_hasCreator_person_0_0.csv \
  --relationships=HAS_TAG=dynamic/comment_hasTag_tag_0_0.csv \
  --relationships=HAS_TAG=dynamic/post_hasTag_tag_0_0.csv \
  --relationships=IS_LOCATED_IN=dynamic/post_isLocatedIn_place_0_0.csv \
  --relationships=STUDY_AT=dynamic/person_studyAt_organisation_0_0.csv \
  --relationships=WORK_AT=dynamic/person_workAt_organisation_0_0.csv
```

参数说明：

- `--nodes=[Label[:Label]...=]<file>`，可重复；
- `--relationships=[Type=]<file>`，可重复；
- `--delimiter <char>`：默认 `|`；
- `--id-type=INTEGER`：当前仅支持 INTEGER，其他值报错；
- 文件路径相对 `--data-dir` 或当前工作目录。

CLI 模式下，文件名不再参与 schema 解析；标签、关系类型完全来自 `--nodes` / `--relationships` 和表头。上面的例子直接对应 Neo4j import 的 converted 数据集（表头已包含 `:ID` / `:LABEL` / `:START_ID` / `:END_ID` 和类型后缀），**文件无需重命名，也无需改查询**。我们 benchmark 中已经生成过一份这样的数据（`/home/dodo/code/fuck/neo4j-local/import-converted`），可直接复用它。

### 3.2 目录扫描模式（兼容模式）

未提供 `--nodes` / `--relationships` 时，回退到现有目录扫描模式，并按第 4 节文件名约定解析。该模式保留给旧数据和简单场景。

### 3.3 总体原则

1. **CLI/表头优先，文件名兜底**：
   - CLI 显式声明的标签/关系类型优先；
   - 表头中出现的特殊标记（`:ID` / `:START_ID` / `:END_ID` / `:LABEL` / 类型后缀）优先决定列语义；
   - 都没有时，回退到当前“文件名 + 首列/前两列”约定。
2. **名称使用原样大小写**：标签名、关系类型名、属性名都按 CLI/表头/文件中的字符串原样创建。LDBC 需要 `Person`，CLI 里写 `Person`，loader 不做大小写猜测。
3. **一个点文件 = 一个 ID 分组**：同一文件内的所有行共享同一个 ID 空间（group），即使行级 `:LABEL` 不同。边文件通过分组名引用 ID 空间。
4. **属性归属主标签**：多标签节点的属性全部挂到“主标签”（`--nodes=A:B=file` 中的第一个标签 A）下，其余标签作为纯标签（空属性集）。这与 Cypher `CREATE (n:A:B {...})` 的现有语义保持一致（属性挂第一个标签）。

## 4. 目录扫描模式的文件识别与命名（兼容）

### 4.1 点文件

识别规则：去掉 `_0_0.csv` 后缀后，剩余部分**不含下划线**的，视为点文件。

- 单标签：`Person_0_0.csv` → 标签 `[Person]`
- 多标签（新增）：`Comment+Message_0_0.csv` → 标签 `[Comment, Message]`，主标签为 `Comment`
- 行级多标签（新增）：文件可以是 `Place_0_0.csv`，表头包含 `:LABEL` 列时，该列每行的值作为额外标签追加到文件级标签之后。

> 兼容性：现有 `person_0_0.csv` 去掉后缀为 `person`（无下划线），仍视为点文件，标签 `[person]`。

### 4.2 边文件

识别规则：去掉 `_0_0.csv` 后缀后，剩余部分**至少含一个下划线**的，视为边文件。解析时：

1. 第一个 `_` 之前为 `src_label`；
2. 最后一个 `_` 之后为 `dst_label`；
3. 中间部分（可包含下划线）为 `edge_type`。

示例：

| 文件名 | src | edge_type | dst |
|---|---|---|---|
| `Person_knows_Person_0_0.csv` | Person | knows | Person |
| `Comment_HAS_CREATOR_Person_0_0.csv` | Comment | HAS_CREATOR | Person |
| `Comment_IS_LOCATED_IN_Country_0_0.csv` | Comment | IS_LOCATED_IN | Country |
| `TagClass_IS_SUBCLASS_OF_TagClass_0_0.csv` | TagClass | IS_SUBCLASS_OF | TagClass |

若表头包含 `:START_ID(Group)` / `:END_ID(Group)`，则 **src/dst 分组以表头为准**，文件名只用于分类。

> 兼容性：现有 `person_knows_person_0_0.csv` 解析结果不变（src=person, edge=knows, dst=person）。

## 5. CSV 表头规范

表头仍是 `|` 分隔的一行。每个列名可以是以下三种之一：

### 5.1 普通属性列

语法：`<property_name>` 或 `<property_name>:<TYPE>`

支持的类型后缀：

| 后缀 | PropertyType | CSV 值解析 |
|---|---|---|
| `STRING` | STRING | 原样 |
| `INT` 或 `LONG` | INT64 | `stoll` |
| `DOUBLE` | DOUBLE | `stod` |
| `BOOL` | BOOL | `true/false` |
| `STRING[]` | STRING_ARRAY | 按 `;` 拆分 |
| `INT[]` 或 `LONG[]` | INT64_ARRAY | 按 `;` 拆分后逐项 `stoll` |
| `DOUBLE[]` | DOUBLE_ARRAY | 按 `;` 拆分后逐项 `stod` |

无后缀时维持现有推断逻辑：采样前 100 行，能全解析为 `INT64` 则 `INT64`，否则 `STRING`。

### 5.2 点文件特殊列

| 表头 | 含义 |
|---|---|
| `:ID` 或 `:ID(GroupName)` | 该列是点 CSV 主键；GroupName 可省略（省略时用文件级分组名） |
| `:LABEL` | 该列每行的值是一个额外标签，追加到文件级标签之后；该列不写入属性 |

规则：
- 若表头有 `:ID`，用该列作为 CSV ID；否则回退为第一列。
- 若表头有 `:LABEL`，每行标签集合 = 文件级标签 + 该行 `:LABEL` 值；否则每行标签集合 = 文件级标签。
- 其余列作为属性（`id` 列是否写入属性？与当前行为保持一致：点文件 ID 列不写入属性）。

### 5.3 边文件特殊列

| 表头 | 含义 |
|---|---|
| `:START_ID` 或 `:START_ID(Group)` | src 顶点 CSV 主键所在列；Group 为 src 分组名（可省略） |
| `:END_ID` 或 `:END_ID(Group)` | dst 顶点 CSV 主键所在列；Group 为 dst 分组名（可省略） |

规则：
- 若表头有 `:START_ID` / `:END_ID`，用其所在列作为 src/dst ID 列；否则回退为第一、第二列。
- 若表头带 `(Group)`，src/dst 分组以表头为准；否则以文件名解析的 `src_label` / `dst_label` 为准。
- 支持 LDBC 旧式表头：`Comment.id` / `Person.id` 这类 `Label.prop` 列名解析为 `:START_ID(Comment)` / `:END_ID(Person)`，从而让 CsvBasic 原始边文件在 CLI 模式下也能直接导入。
- 其余列作为边属性。

### 5.4 示例：LDBC 目录片段

点文件 `Place_0_0.csv`：

```text
id:ID|name:STRING|url:STRING|:LABEL
0|India|http://dbpedia.org/resource/India|Country
```

点文件 `Comment+Message_0_0.csv`：

```text
id:ID|creationDate:LONG|locationIP:STRING|browserUsed:STRING|content:STRING|length:INT
618475290625|1313591219961|46.16.217.105|Chrome|yes|3
```

点文件 `Person_0_0.csv`：

```text
id:ID|firstName:STRING|lastName:STRING|gender:STRING|birthday:LONG|creationDate:LONG|locationIP:STRING|browserUsed:STRING|speaks:STRING[]|email:STRING[]
933|Mahinda|Perera|male|628646400000|1266161530447|119.235.7.103|Firefox|si;en|Mahinda933@boarderzone.com
```

边文件 `Comment_HAS_CREATOR_Person_0_0.csv`：

```text
:START_ID(Comment)|:END_ID(Person)
618475290625|933
```

## 6. ID 空间与分组

引入 **group（ID 分组）** 概念，替代当前“按 label 映射 ID”的做法。

- 每个点文件对应一个 group，group 名 = 文件名去掉 `_0_0.csv` 后缀（例如 `Comment+Message`、`Place`）。
- loader 维护：
  - `label_to_group: label_name -> group_name`：文件级标签和 `:LABEL` 列出现过的每个标签都映射到该 group；
  - `group_id_map: group_name -> (csv_id -> VertexId)`：每个 group 的 CSV ID 到实际 VertexId 的映射。
- 边文件解析时，通过 `src_label` / `dst_label`（或表头 `:START_ID(Group)` / `:END_ID(Group)`）查 `label_to_group`，再查 `group_id_map` 得到真实 VertexId。
- 若 `src_label` / `dst_label` 在 `label_to_group` 中不存在，报错并退出（比当前“跳过边”更严格，避免静默丢数据；也可通过 `--skip-missing-edges` 保留旧行为，本期先报错）。

> 与当前实现的区别：当前 `CsvIdMap` 是 `(label, csv_id)`。由于同一个 `csv_id` 在不同 group 中可能重复，而多标签节点的多个 label 会映射到同一个 group，所以必须按 group 存储，否则 `Comment` 和 `Message` 会得到两份独立映射。

## 7. Schema 构建

`buildLabelSchemas` 与 `buildEdgeTypeSchemas` 扩展为解析新表头：

1. 解析每个列名：
   - 识别特殊列 `:ID` / `:LABEL` / `:START_ID` / `:END_ID` 及 `(Group)`；
   - 识别类型后缀 `:TYPE`；
2. 点文件：
   - `LabelSchema` 增加 `std::vector<std::string> labels`（文件级标签）；
   - 若表头有 `:LABEL`，标记 `has_row_label_column`。
3. 边文件：
   - `EdgeTypeSchema` 保持按 `edge_type` 合并；
   - 记录 `src_label` / `dst_label` 默认值（可被表头覆盖）；
4. 类型推断：
   - 有显式类型后缀的列，跳过采样推断，直接使用声明类型；
   - 无后缀列沿用现有推断。

## 8. 装载流程

### 8.1 CLI 模式（主）

```
1. 解析 --nodes / --relationships / --delimiter / --id-type
2. 读取每个文件表头，构建 LabelSchema / EdgeTypeSchema：
   - 标签列表来自 --nodes 的 Label[:Label...]
   - 关系类型来自 --relationships 的 Type
   - 列语义来自表头（:ID / :LABEL / :START_ID / :END_ID / 类型后缀）
3. 创建所有标签（DDL，含行级 :LABEL 值）
4. 创建所有关系类型（DDL）
5. 写点文件：
   a. 每行确定完整标签集合（--nodes 标签 + 行级 :LABEL）
   b. 按标签集合分组批量插入（同组复用一条 RPC）
   c. 记录 group -> (csv_id -> VertexId)
6. 为每个标签的 ID 属性创建唯一索引
7. 写边文件：
   a. 解析 src/dst 分组（表头 :START_ID(Group)/:END_ID(Group) 优先，否则用 --relationships 的 src/dst 标签？）
   b. 通过 label_to_group + group_id_map 解析 VertexId
   c. 批量插入边
```

> 注：CLI 模式没有文件名可推导 src/dst 标签，因此边文件必须有表头 `:START_ID(Group)` / `:END_ID(Group)`（或 LDBC 旧式 `Label.id` 表头）；若没有，则报错。

### 8.2 目录扫描模式（兼容）

```
1. 扫描目录，按第 4 节命名规则分类点/边文件
2. 解析表头，构建 LabelSchema（含多标签与行级标签信息）与 EdgeTypeSchema
3. 创建所有文件级标签 + 行级标签（DDL）
4. 创建所有关系类型（DDL）
5. 并发/串行写点文件：
   a. 每行确定完整标签集合（文件级 + 行级 :LABEL）
   b. 按标签集合分组批量插入（同组复用一条 RPC）
   c. 记录 group -> (csv_id -> VertexId)
6. 为每个标签的 ID 属性创建唯一索引（CREATE UNIQUE INDEX）
7. 并发/串行写边文件：
   a. 解析 src/dst 分组（表头优先，文件名兜底）
   b. 通过 label_to_group + group_id_map 解析 VertexId
   c. 批量插入边
```

## 9. RPC 与存储接口扩展

当前 loader 通过 RPC `batchInsertVertices(label_name, records)` 写点，**该 RPC 目前只支持单标签**。多标签点插入需要扩展这条 RPC 路径，而不是导入后再用 `MATCH ... SET n:Label` 补标签（那样既慢又违背批量导入的初衷）。具体扩展如下：

1. **Thrift**：`VertexRecord` 增加字段 `2: list<string> labels;`
   - 语义：该记录的完整标签列表（顺序有意义，第一个为主标签/属性标签）；
   - 为空时回退为批次参数 `label_name`（旧客户端兼容）。
2. **GraphService::batchInsertVertices**：
   - 解析 `label_name` 为主标签；
   - 对每条 record，若 `labels` 非空，解析额外标签的 `LabelId`（不存在则报错）；
   - 构造 `vector<pair<LabelId, Properties>>`：主标签携带属性，额外标签携带空属性；
3. **AsyncGraphDataStore::batchInsertVertices**：
   - `BatchVertexEntry` 增加 `std::vector<std::pair<LabelId, Properties>> label_props`（或额外标签列表）；
   - 底层 `insertVertex` 已支持一次写入多个标签的属性，只需把 span 传过去。
4. **RPC 客户端**：`EuGraphRpcClient::batchInsertVertices` 增加可选 `labels` 参数。

## 10. 错误处理

| 场景 | 处理 |
|---|---|
| CLI 模式下边文件表头没有 `:START_ID(Group)` / `:END_ID(Group)` | 报错退出（CLI 模式无法从文件名推导 src/dst 分组） |
| 文件名既不是点文件也不是边文件 | 跳过（与当前一致），但输出 warning |
| 表头 `:ID` / `:START_ID` / `:END_ID` 列不存在 | 报错退出 |
| 显式类型后缀解析失败（如 `foo:INT` 但值为 `abc`） | 报错退出，并指出文件/行号 |
| 边 src/dst label 找不到对应 group | 报错退出（列出边文件和 label 名） |
| `:LABEL` 列出现空值 | 忽略该额外标签（视为无） |
| 多标签文件的主标签与旧格式 `{label}_0_0.csv` 单标签行为 | 完全一致 |

## 11. 向后兼容性

- 旧点文件 `person_0_0.csv`：去掉后缀无下划线 → 点文件，单标签 `person`，首列 ID，全部列作为属性推断，行为不变。
- 旧边文件 `person_knows_person_0_0.csv`：新解析方式得到相同 src/edge/dst，行为不变。
- 旧表头无特殊列：无 `:ID` / `:LABEL` / 类型后缀，回退逻辑与当前一致。
- 旧 RPC 客户端：`VertexRecord.labels` 为空，服务端回退 `label_name`，行为不变。

## 12. 测试计划

1. **单元测试**（`tests/test_loader_integration.cpp` 扩展或新增）：
   - 文件名解析：单标签、多标签 `A+B`、边类型含下划线、旧格式；
   - 表头解析：`:ID`、`:ID(Group)`、`:LABEL`、`:START_ID(Group)`、`:END_ID(Group)`、类型后缀；
   - 类型解析：`STRING` / `INT` / `LONG` / `DOUBLE` / `BOOL` / `STRING[]` 及回退推断。
2. **集成测试**（loader + server）：
   - `Comment+Message` 文件导入后，`MATCH (n:Comment)` 与 `MATCH (n:Message)` 都能查到节点，`labels(n)` 包含两个标签；
   - `Place` + `:LABEL` 导入后，`MATCH (c:City)` / `(c:Country)` 分别正确；
   - `Comment_HAS_CREATOR_Person` 边导入后，`MATCH (:Comment)-[:HAS_CREATOR]->(:Person)` 返回正确行数；
   - 旧格式目录导入后行为与当前一致（回归）。
3. **Benchmark 验证**：
   - 用 LDBC sf0.1 原始 CsvBasic 目录（或经过最小转换的目录）导入 eugraph，确认 LDBC 查询无需改写关系类型/`:Message` 标签即可执行。

## 13. 后续可选项（不在本期）

- **大小写归一化选项**：如 `--normalize-labels=upper`，把首字母小写的 LDBC 原始文件名自动映射为大写标签/关系类型；默认不开启。
- **更完整的 CSV 解析**：引号转义、多字符分隔符、`--array-delimiter` 选项。
- **原始 CsvBasic 行级标签支持**：如 `--label-column Place=type` + `--label-map=Place:type:city=City,country=Country`，允许不经过 Neo4j convert 直接导入原始 CsvBasic；默认不开启。
