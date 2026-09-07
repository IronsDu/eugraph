# CSV Loader 使用指南

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

批量 CSV 数据装载工具（`eugraph-loader`），通过 RPC 连接 eugraph server，将 CSV 数据导入图数据库。

设计文档见 [loader-design.md](../design/loader-design.md)。

---

## 启动

```bash
eugraph-loader --host 127.0.0.1 --port 9090 --data-dir ./csv-data
```

并行装载示例：

```bash
eugraph-loader --host 127.0.0.1 --port 9090 --data-dir ./csv-data \
    --batch-size 500 --eventbase-threads 4 --concurrency 4
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | 127.0.0.1 | Server 地址 |
| `--port` | 9090 | Server 端口 |
| `--data-dir` | **必填** | CSV 文件根目录 |
| `--nodes` | 无 | 点文件映射 `Label[:Label...]=file`，可重复 |
| `--relationships` | 无 | 边文件映射 `TYPE=file`，可重复 |
| `--delimiter` | `\|` | CSV 分隔符，当前仅支持 `\|` |
| `--batch-size` | 500 | 每 RPC 批次的记录数 |
| `--eventbase-threads` | 1 | 创建多少个独立 RPC EventBase 客户端/连接 |
| `--concurrency` | 1 | 最多并行装载多少个 CSV 文件；`--loader-concurrency` 是同义别名 |

## 两种使用方式

### 1. CLI 显式映射（主模式）

用 `--nodes` / `--relationships` 显式声明文件与标签/关系类型的映射，文件名不参与 schema 解析。适合 LDBC/Neo4j import 风格数据：

```bash
eugraph-loader --host 127.0.0.1 --port 9090 \
  --data-dir /path/to/neo4j-converted \
  --delimiter '|' \
  --nodes=Place=static/place_0_0.csv \
  --nodes=Organisation=static/organisation_0_0.csv \
  --nodes=Comment:Message=dynamic/comment_0_0.csv \
  --nodes=Post:Message=dynamic/post_0_0.csv \
  --relationships=IS_PART_OF=static/place_isPartOf_place_0_0.csv \
  --relationships=KNOWS=dynamic/person_knows_person_0_0.csv
```

`Comment:Message` 表示：该文件每个节点同时打上 `Comment` 和 `Message` 两个标签；第一个标签 `Comment` 为主标签，属性写入 `Comment` 下。若某点文件表头有 `:LABEL` 列，则行级标签优先成为主标签（如 Place 行按 `type` 得到 `Country`，属性写入 `Country` 下，便于创建 `Country(name)` 索引）。

### 2. 目录扫描模式（兼容）

不提供 `--nodes` / `--relationships` 时，扫描 `--data-dir` 下的 CSV 文件，按文件名约定分类：

- 点文件：`{labels}_0_0.csv`，例如 `Person_0_0.csv`、`Comment+Message_0_0.csv`
- 边文件：`{srcLabel}_{edgeType}_{dstLabel}_0_0.csv`，其中 `edgeType` 可以包含下划线，例如 `Comment_HAS_CREATOR_Person_0_0.csv`

## 数据格式

### 目录结构

```
data-dir/
├── static/          # 静态数据
└── dynamic/         # 动态数据
```

目录结构对 loader 不是强制的，路径由 `--data-dir` 和 CLI 参数决定。

### CSV 格式

分隔符默认 `|`，首行为表头。

**点文件**：
- 若无 `:ID` 列，第一列为 CSV 主键（INT64），且作为 `id` 属性写入；
- 若表头有 `:ID` 或 `:ID(Group)`，用该列作为 CSV 主键，仍作为属性写入；
- 若表头有 `:LABEL`，该列每行的值作为行级标签，且**行级标签优先成为主标签**；
- 其余列作为属性。

**边文件**：
- 若无 `:START_ID` / `:END_ID` 列，前两列为 src/dst 的 CSV 主键；
- 若表头有 `:START_ID(Group)` / `:END_ID(Group)`，用其所在列作为 src/dst 主键，并用 `Group` 作为分组名；
- 其余列作为边属性。

### 表头类型后缀

| 后缀 | 说明 |
|---|---|
| `name:STRING` | 字符串属性 |
| `length:INT` / `creationDate:LONG` | 64 位整数属性 |
| `score:DOUBLE` | 浮点属性 |
| `flag:BOOL` | 布尔属性 |
| `speaks:STRING[]` | 字符串数组，CSV 中按 `;` 分隔 |
| `ids:INT[]` / `ids:LONG[]` | 整数数组 |
| `values:DOUBLE[]` | 浮点数组 |

无后缀列自动推断：采样前 100 行，能全解析为 INT64 则 INT64，否则 STRING。

### Neo4j converted 数据集示例

Neo4j import 转换后的 LDBC CSV 可直接导入：

```text
点文件 Place_0_0.csv:
id:ID|name:STRING|url:STRING|:LABEL
0|India|http://dbpedia.org/resource/India|Country

点文件 Person_0_0.csv:
id:ID|firstName:STRING|lastName:STRING|gender:STRING|birthday:LONG|creationDate:LONG|locationIP:STRING|browserUsed:STRING|speaks:STRING[]|email:STRING[]
933|Mahinda|Perera|male|628646400000|1266161530447|119.235.7.103|Firefox|si;en|Mahinda933@boarderzone.com

边文件 comment_hasCreator_person_0_0.csv:
:START_ID(Comment)|:END_ID(Person)
618475290625|933
```
