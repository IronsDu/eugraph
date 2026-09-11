# Loader 设计

> [当前实现] 参见 [README.md](README.md) 返回文档导航

使用文档见 [loader.md](../usage/loader.md)。

---

## 1. 总体形态

`eugraph-loader` 通过 RPC 连接运行中的 eugraph server，将 CSV 数据批量写入图数据库。支持两种使用方式：

1. **CLI 显式映射（主模式）**：用户通过 `--nodes` / `--relationships` 显式声明文件与标签/关系类型的映射，文件名不参与 schema 解析。这是对标 `neo4j-admin import` 的方式。
2. **目录扫描模式（兼容模式）**：未提供 `--nodes` / `--relationships` 时，按文件命名约定自动分类点/边文件。保留给旧数据和简单场景。

两种模式共享同一套 CSV 表头解析、属性类型解析、批量 RPC 写入逻辑。

## 2. CLI 显式映射

```bash
eugraph-loader --host 127.0.0.1 --port 9090 \
  --data-dir /path/to/csv-root \
  --delimiter '|' \
  --nodes=Person=dynamic/person_0_0.csv \
  --nodes=Comment:Message=dynamic/comment_0_0.csv \
  --nodes=Place=static/place_0_0.csv \
  --relationships=KNOWS=dynamic/person_knows_person_0_0.csv \
  --relationships=HAS_CREATOR=dynamic/comment_hasCreator_person_0_0.csv
```

参数：

| 参数 | 含义 |
|---|---|
| `--nodes=[Label[:Label]...=]<file>` | 点文件映射，可重复。`Label` 列表第一个为主标签，属性存放在主标签下。 |
| `--relationships=[Type=]<file>` | 边文件映射，可重复。关系类型名可为任意字符串（含下划线）。 |
| `--delimiter <char>` | CSV 分隔符，当前仅支持 `\|`。 |
| `--data-dir` | 文件路径基准目录；`--nodes` / `--relationships` 中的相对路径基于此。 |

## 3. 目录扫描模式（兼容）

未指定 `--nodes` / `--relationships` 时，扫描 `--data-dir` 下的 CSV 文件并分类：

- 去掉 `_0_0.csv` 后缀后：
  - 不含 `_`：点文件，标签部分可用 `+` 连接多个标签。例如 `Comment+Message_0_0.csv` → 标签 `[Comment, Message]`，主标签为 `Comment`。
  - 含 `_`：边文件。按第一个 `_` 切出 `src`，按最后一个 `_` 切出 `dst`，中间为 `edge_type`。例如 `Comment_HAS_CREATOR_Person_0_0.csv` → `(Comment)-[HAS_CREATOR]->(Person)`。

旧格式 `person_0_0.csv`、`person_knows_person_0_0.csv` 继续兼容。

## 4. CSV 表头

分隔符默认 `|`，首行为表头。表头优先于文件名约定。

### 4.1 点文件

| 表头 | 含义 |
|---|---|
| `:ID` 或 `:ID(Group)` | 该列是点 CSV 主键；`Group` 可省略。该列仍作为普通属性写入（属性名通常为 `id`，类型 INT64）。 |
| `:LABEL` | 该列每行的值作为该行的行级标签。**行级标签优先成为主标签**，文件级标签排在其后。该列不写入属性。 |
| `name:TYPE` | 带类型后缀的属性列。 |

示例：

```text
id:ID|name:STRING|url:STRING|:LABEL
0|India|http://dbpedia.org/resource/India|Country
```

该行顶点标签为 `[Country, Place]`，属性存放在 `Country` 下。

### 4.2 边文件

| 表头 | 含义 |
|---|---|
| `:START_ID` / `:START_ID(Group)` | src 顶点 CSV 主键所在列；`Group` 为 src 分组名。 |
| `:END_ID` / `:END_ID(Group)` | dst 顶点 CSV 主键所在列；`Group` 为 dst 分组名。 |
| `name:TYPE` | 边属性列。 |

兼容 LDBC 旧式表头：`Comment.id` / `Person.id` 会被解析为 `:START_ID(Comment)` / `:END_ID(Person)`。

### 4.3 属性类型后缀

| 后缀 | PropertyType | CSV 值解析 |
|---|---|---|
| `STRING` | STRING | 原样 |
| `INT` / `LONG` | INT64 | `stoll` |
| `DOUBLE` | DOUBLE | `stod` |
| `BOOL` | BOOL | `true/false` |
| `STRING[]` | STRING_ARRAY | 按 `;` 拆分 |
| `INT[]` / `LONG[]` | INT64_ARRAY | 按 `;` 拆分后逐项 `stoll` |
| `DOUBLE[]` | DOUBLE_ARRAY | 按 `;` 拆分后逐项 `stod` |

无后缀列保留类型推断：采样前 100 行，能全解析为 INT64 则 INT64，否则 STRING。

## 5. 多标签写入

Loader 通过扩展后的 RPC `batchInsertVertices` 一次写入多标签顶点：

- `VertexRecord` 增加 `labels` 字段，表示完整标签列表，顺序有意义；
- 第一个标签为主标签，属性写入主标签的属性表；
- 其余标签为附加标签，属性表为空；
- `:LABEL` 行级标签优先成为主标签，便于后续在 `Country` / `City` / `Company` / `University` 等标签上创建有效索引。

属性值按 **属性名** 映射到主标签的属性 ID，因此两个文件共享同一主标签但列顺序不同时，属性不会错位。

## 6. ID 空间与分组

- 每个点文件对应一个 group（ID 空间），group 名取自文件名去掉 `_0_0.csv` 后的部分。
- loader 维护：
  - `label_to_group: label_name -> group_name`
  - `group_id_map: group_name -> (csv_id -> VertexId)`
- 边文件解析时，通过 src/dst 标签名查 `label_to_group`，再查 `group_id_map` 得到真实 VertexId。

## 7. 装载流程

```
1. 解析 CLI 参数（CLI 模式）或扫描目录（扫描模式）
2. 读取每个文件表头，构建 LabelSchema / EdgeTypeSchema
3. 合并同一标签在多个文件中的属性定义（按属性名去重）
4. 创建所有标签（DDL）
5. 创建所有关系类型（DDL）
6. 写点文件：
   a. 每行确定完整标签集合（文件级 + 行级 :LABEL，行级优先为主标签）
   b. 按主标签分组批量插入（同组复用一条 RPC）
   c. 记录 group -> (csv_id -> VertexId)
7. 为每个标签的 ID 属性创建唯一索引（CREATE UNIQUE INDEX）
8. 写边文件：
   a. 解析 src/dst 分组（表头优先，文件名兜底）
   b. 通过 label_to_group + group_id_map 解析 VertexId
   c. 批量插入边
```

默认串行（`--concurrency 1`）。配置 `--eventbase-threads N --concurrency M` 后，
loader 创建 `N` 个独立 RPC EventBase 客户端，并用最多 `M` 个工作线程并发处理 CSV 文件：
先并行装载 vertex 文件，待点映射全部就绪后再并行装载 edge 文件。

## 8. 服务端扩展

Loader 使用两个批量 RPC 端点：

- **`batchInsertVertices`**：`label_name` 为主标签，`VertexRecord.labels` 为完整标签列表；服务端批量分配 VertexId，按多标签写入顶点，返回 VertexId 列表。
- **`batchInsertEdges`**：接收 edge_label_name + 记录列表（含已解析的 src/dst VertexId）→ 批量分配 EdgeId → 写入边。

两个端点均使用独立事务（不参与外层 Cypher 事务）。

> 并发装载要求服务端能够安全地并发分配 VertexId/EdgeId。`AsyncGraphMetaStore` 对
> `nextVertexIdRange()` / `nextEdgeIdRange()` 的计数更新和 `M|next_ids` 持久化做了互斥保护。
> 服务端采用 ID 缓存批量分配：内存中缓存一段已持久化的 VertexId/EdgeId 区间，
> 缓存耗尽时才从持久化高水位批量预留一批（当前 16384 个）并持久化一次。
>
> 批量写入路径复用 `TxnState` 中缓存的 WT cursor（`tablePutTxn`），
> 避免每条边/点重复 `open_cursor` 与 dhandle/malloc 开销。

## 9. 文件结构

```
src/program/loader/
  loader_main.cpp       # 入口：参数解析、流程编排
  csv_loader.hpp/cpp    # CLI/目录扫描、CSV 解析、Schema 构建、数据加载
```

## 10. 错误处理

| 场景 | 处理方式 |
|------|---------|
| Server 连接失败 | 报错退出 |
| CLI 模式边文件表头没有 `:START_ID` / `:END_ID` | 报错退出（CLI 模式无法从文件名推导 src/dst 分组） |
| 表头 `:ID` / `:START_ID` / `:END_ID` 列不存在 | 报错退出 |
| 显式类型后缀解析失败 | 报错退出，并指出文件 |
| 标签/关系类型已存在 | Handler 返回 id=0，Loader 继续执行（幂等） |
| 批量写入失败 | 回滚当前批次，报错退出 |
| 边的 src/dst 顶点未找到 | 跳过该边（计数 skipped），继续加载 |
| `:LABEL` 列出现空值 | 忽略该行级标签 |
