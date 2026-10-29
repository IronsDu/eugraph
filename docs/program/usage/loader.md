# CSV Loader 使用指南

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

批量 CSV 数据装载工具（`eugraph-loader`），通过 RPC 连接 eugraph server，将 CSV 数据导入图数据库。

**装载的一切声明都来自 schema JSON 文件**（标签、属性名与类型、主键、行级标签、边端点、分隔符、时间格式）。
命令行只控制「怎么跑」（连哪儿、批多大、几路并发），不含任何数据声明 —— 因此**同一份 schema 必然装出同一张图**，
命令行历史不会影响结果。

设计文档见 [loader-design.md](../design/loader-design.md)（权威版本；含当前限制 §12）。

---

## 启动

```bash
eugraph-loader --schema ./loader-schema.json --data-dir ./csv-data --host 127.0.0.1 --port 9090
```

启动时会**先连服务器并做一次真实 RPC 握手**（`listGraphs`），再解析 schema、扫数据目录 ——
端口写错或服务没起会立刻报 `Cannot reach server ...`，不会白跑完前面几步才失败。

并行装载示例：

```bash
eugraph-loader --schema ./loader-schema.json --data-dir ./csv-data \
    --host 127.0.0.1 --port 9090 \
    --batch-size 2000 --rpc-connections 2 --parallel-files 2
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--schema` | 无（**必填**） | schema JSON 路径，唯一的声明入口 |
| `--data-dir` | 无（**必填**） | CSV 根目录；schema 里 `file` 的相对路径以此为基准 |
| `--host` | 127.0.0.1 | Server 地址 |
| `--port` | 9090 | Server 端口 |
| `--batch-size` | 500 | 每 RPC 批次的记录数 |
| `--rpc-connections` | 1 | 到 server 的并发 RPC 连接数；每个连接对应一个独立 EventBase 线程 |
| `--parallel-files` | 1 | 最多同时装载多少个 CSV 文件 |
| `--graph` | `default` | 装载到哪张图；**不存在则自动创建**。`neo4j` / `system` 是服务端的 default 别名，会被拒绝（否则写入与查询会落到不同的图） |
| `--progress` | `auto` | 进度显示：`auto`（TTY 上原地刷新进度块，输出被重定向时退化为日志）/ `plain`（只打日志行）/ `off`（完全静默，脚本用） |
| `--no-schema-strict` | 关 | 打开后：schema 未声明的列回落到采样推断（默认报错并列出列名） |

> **没有** `--nodes` / `--relationships` / `--pk` / `--types` / `--date-format` / `--delimiter` /
> `--skip-undeclared-check`：这些都是数据声明，已全部收进 schema 文件。传入会直接报未知参数。

### 目标图

`--graph <name>` 决定装载到哪张图：**不存在则自动创建**（服务端 `createGraph` 幂等，重复装载直接复用）。

- schema 里**不能**写图名（写了会报错并提示改用 `--graph`）：装到哪张图是运行期决定，不是数据声明；
- `neo4j` / `system` 是服务端对 `default` 的别名，作为 `--graph` 会被拒绝 —— 拿它建图会建出一张同名、但查询永远不会走到的图。

## schema 文件

### 顶层字段

| 字段 | 说明 |
|------|------|
| `nodes` | 顶点：`标签名 → [文件条目, ...]`（原字段名 `labels`，已改名，老写法直接报错） |
| `relationships` | 边：`边类型名 → [文件条目, ...]` |
| `before_load` | 装载**前**要执行的 Cypher 语句数组（schema 的标签/索引 DDL 之后、任何数据写入之前） |
| `after_load` | 装载**后**要执行的 Cypher 语句数组（点与边全部写完之后），用来声明"装载完成时的状态"，典型是建索引 |
| `delimiter` | 字段分隔符**全局默认**（默认 `"|"`；支持多字符，如 `"::"`；`"\t"`/`"tab"` 表示制表符） |
| `date_format` | 时间列解析格式**全局默认**（默认 `"epoch_ms"`；可选 `epoch_s`/`epoch_us`/`epoch_ns`/`iso`） |
| `undeclared_files` | 数据目录里有未声明 CSV 时的行为：`"error"`（默认，报错并列出文件）或 `"ignore"` |
| `ignore` | 属于本数据集但不参与初始装载的文件（如 LDBC 的 `updateStream_*.csv`） |

### 装载前 / 装载后的语句

schema 是「装载完成时的完整状态」的声明，因此索引这类 DDL 可以一并写进 schema，不必装完再进 shell 手工执行。

```jsonc
{
  "nodes": { /* ... */ },
  "relationships": { /* ... */ },

  "before_load": [
    "CREATE INDEX idx_Person_name FOR (n:Person) ON (n.name)"      // 数据之前建：装载期由写路径维护
  ],
  "after_load": [
    "CREATE INDEX idx_Message_id FOR (n:Message) ON (n.id)",        // 典型：点/边装完后建索引
    { "statement": "CREATE INDEX idx_Tag_name FOR (n:Tag) ON (n.name)", "on_error": "ignore" }
  ]
}
```

规则：

- 元素是**字符串**，或 `{"statement": "...", "on_error": "fail" | "ignore"}`；默认 `fail`。
- 单连接、按数组顺序执行；`before_load` 在 schema 的标签 / 边类型 / 主键索引 DDL **之后**、任何数据之前，`after_load` 在点与边全部写完之后。
- 任意一条失败即中止装载，报错包含阶段、序号与语句原文；`on_error: "ignore"` 只告警并继续。
- 幂等：引擎没有 `IF NOT EXISTS`，**在同一张图上重跑**时已存在的索引会让 `CREATE INDEX` 失败 —— 给该条加 `on_error: "ignore"` 即可（主键索引由 loader 按名字判断，重跑无需额外配置）。
- 写的是普通 Cypher，`CREATE INDEX` / `CREATE UNIQUE INDEX` / `DROP INDEX` / `MATCH ... DETACH DELETE` 等都能用。

### 命名规则（容易踩的一点）

| 位置 | 用哪种名字 |
|------|-----------|
| `columns` 的键、`pk` | **图里的属性名** |
| `src` / `dst` / `label.header` / `columns.*.header` | **CSV 里的列名** |

### 文件条目

```jsonc
{
  "delimiter": "|",              // 全局默认；文件条目里可覆盖
  "date_format": "epoch_ms",     // 全局默认；文件条目里可覆盖
  "undeclared_files": "error",
  "nodes": {
    "Person": [{
      "file": "dynamic/person_0_0.csv",
      "pk": "id",                                   // 属性名；复合主键写成数组 ["tenantId","userId"]
      "columns": {
        "id": "INT64",                              // 属性名 → 类型（列名同名时可简写）
        "creationDate": "DATETIME",
        "email": "STRING[]",
        "kind": { "header": "type", "type": "STRING" }   // 改名：属性 kind 来自列 type
      },
      "label": [ { "header": "type", "case": "capitalize" } ] // 行级标签来源（数组）：列 type 的值首字母大写后作**追加**标签
    }]
  },
  "relationships": {
    "KNOWS": [{
      "file": "dynamic/person_knows_person_0_0.csv",
      "src": "Person.id",                           // 列名（可带目标标签前缀）；复合主键写成数组
      "dst": "Person.id",
      "src_label": "Person", "dst_label": "Person",
      "columns": { "creationDate": "INT64" }
    }]
  }
}
```

要点：

- **主键可选**：不写 `pk` 也能导入，只是不能用它解析边端点。写了 `pk` 就会建一个 `UNIQUE` 索引，
  装载期由它解析边端点，同时让 `MATCH (n:Person {id: 5})` 走索引。
- **主键冲突是 first-wins**：重复主键的顶点被跳过并告警，不阻断装载。
- **行级标签是追加标签**，不抢主标签：`place` 文件里 `type=city` 的顶点带 `[Place, City]`，
  属性始终写在 schema 键（`Place`）下。
- **`label` 是数组**：一个文件可以有**多个**标签来源，结果取**并集去重**（与主标签同名则丢弃）。
  每个来源恰好是三种之一（**互斥**，同时给出会报错）：

  | 写法 | 含义 |
  |---|---|
  | `{ "derived": ["Message"] }` | 静态标签，不读 CSV；可列多个 |
  | `{ "header": "type", "case": "capitalize" }` | 该列每个非空值产出一个标签 |
  | `{ "index": 6, "case": "capitalize" }` | 按列号（0 基）；**表头重名时必须用这个** |

  ```jsonc
  // 静态 + 列 混合；一行可同时得到 Message 与 City
  "label": [ { "derived": ["Message"] }, { "header": "type", "case": "capitalize" } ]
  // neo4j 的 :LABEL|:LABEL 惯例：两列同名，只能靠列号区分
  "label": [ { "index": 5 }, { "index": 6 } ]
  ```

  `case` 可选 `none`（默认）/ `capitalize` / `lower`。
- **边端点必须声明 `src_label`/`dst_label`**，且目标标签必须声明主键 —— 否则启动即报错，
  不会等到装载时静默跳过。

### 类型写法

| 配置写法 | 说明 | CSV 值解析 |
|---|---|---|
| `BOOL` | 布尔 | `true`/`false` |
| `INT` / `INT64` / `LONG` | 64 位整数 | `stoll` |
| `DOUBLE` | 浮点 | `stod` |
| `STRING` | 字符串 | 原样 |
| `INT[]` / `LONG[]` | 整数数组 | `;` 拆分后逐项解析 |
| `DOUBLE[]` | 浮点数组 | `;` 拆分后逐项解析 |
| `STRING[]` | 字符串数组 | `;` 拆分 |
| `DATE` | 日期 | 按 `date_format` |
| `DATETIME` | 本地时间（无时区） | 按 `date_format` |
| `DATETIME_WITH_TZ` | 带时区时间 | 按 `date_format` |
| `TIME` / `DURATION` | 时间 / 时长 | 见设计文档 |

`date_format` 为 `epoch_*` 时，纯数字按对应单位解释；否则按 ISO-8601 文本解析；
显式写 `"iso"` 表示一律按文本解析。

### CSV 读取能力

- UTF-8 BOM 自动剥离（Excel「另存为 CSV」默认带）；
- RFC 4180 引号：`"a|b"` 是一个字段，`""` 解转义为 `"`，引号内可含分隔符与换行；
- 分隔符按**整体**匹配，支持多字符（`"::"`）；
- 每行做列数校验，不等于表头列数即报错，消息含 `文件:行号:期望:实际`。

## 进度显示

`--progress auto`（默认）在终端里画一块原地刷新的进度：

```text
[vertices] 3/8 labels  45% [===========>  ···        ] 147,000/327,588 rows 12.3s avg 11.9k/s
  ok Person   person_0_0.csv  9,890 rows (written 9,890)  2.1s      ← 装完定格、换行装下一个
  ·· Place    place_0_0.csv  78% [=============>  ···  ] 1,140/1,460  1.8s 633/s   ← 正在装载：原地刷新
```

视觉：紫色渐变 `=` + `==>` 箭头 + 未填充区的暗色流光，行尾是「已耗时 / 均速 / 实时速度」；
交互期隐藏光标、结束恢复。`NO_COLOR=1` 或 `TERM=dumb` 自动去色，非 TTY 完全不输出控制符。

- 开跑前先打印**装载计划**（每个标签/边类型的文件数与总行数），所以一开始就知道要装哪些标签、哪些边；
- 总进度固定第一行；每个文件一行，正在装载的行原地刷新（滑动方块跑马灯），装完定格保留；`--parallel-files>1` 时最多 N 行同时刷新；
- 行数分母来自**启动时预扫描**：无引号文件按物理行快速计数（`io::LineReader`），出现引号字段时回落到与解析相同的精确计数（带引号的换行不会被多数）；畸形 CSV 在动 DDL 之前就报错退出；
- 输出被重定向（管道 / CI）时自动退化为逐文件日志行，不输出任何 ANSI 控制符；`--progress off` 完全静默。

## 与 neo4j-admin import 的对照

`neo4j-admin import` 的 `--nodes` / `--relationships` 在这里的等价物就是 schema 的
`nodes` / `relationships`；`headers.txt` 的列类型声明对应 `columns`；
`:LABEL` 列对应 `label`；`--delimiter` 对应顶层 `delimiter`。

**id 类型**：参考实现固定用 `import-to-neo4j.sh --id-type=INTEGER`，因此 schema 里
`id:ID(...)` 对应 `INT64`（见 [ldbc-sf01-composite.schema.json](../design/ldbc-sf01-composite.schema.json)）。
本地 neo4j 曾用默认 `--id-type=STRING` 导入（`id` 为字符串），2026-10-29 已就地转为
`INTEGER`，与参考实现一致。不要把 loader schema 改成 `STRING`：那会破坏 `ORDER BY id`、
`id + 1` 等数值语义；对照时驱动侧用 `id_type=long` 即可。

sf0.1 语料的现成配置：
- CsvBasic：[ldbc-sf01.schema.json](../design/ldbc-sf01.schema.json)
- CsvComposite（`language` → `speaks`、含 `email`）：[ldbc-sf01-composite.schema.json](../design/ldbc-sf01-composite.schema.json)
（数据一致性可用 `scripts/verify_loader_vs_neo4j.py` 核对，判据取自
`ldbc_snb_interactive_v1_impls/cypher/scripts`）。
