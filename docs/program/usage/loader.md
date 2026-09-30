# CSV Loader 使用指南

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

批量 CSV 数据装载工具（`eugraph-loader`），通过 RPC 连接 eugraph server，将 CSV 数据导入图数据库。

**装载的一切声明都来自 schema JSON 文件**（标签、属性名与类型、主键、行级标签、边端点、分隔符、时间格式）。
命令行只控制「怎么跑」（连哪儿、批多大、几路并发），不含任何数据声明 —— 因此**同一份 schema 必然装出同一张图**，
命令行历史不会影响结果。

设计文档见 [loader-primary-key-design.md](../design/loader-primary-key-design.md)（主键化与类型配置）。

---

## 启动

```bash
eugraph-loader --schema ./loader-schema.json --data-dir ./csv-data --host 127.0.0.1 --port 9090
```

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
| `--no-schema-strict` | 关 | 打开后：schema 未声明的列回落到采样推断（默认报错并列出列名） |

> **没有** `--nodes` / `--relationships` / `--pk` / `--types` / `--date-format` / `--delimiter` /
> `--skip-undeclared-check`：这些都是数据声明，已全部收进 schema 文件。传入会直接报未知参数。

## schema 文件

### 顶层字段

| 字段 | 说明 |
|------|------|
| `labels` | 顶点：`标签名 → [文件条目, ...]` |
| `relationships` | 边：`边类型名 → [文件条目, ...]` |
| `delimiter` | 字段分隔符**全局默认**（默认 `"|"`；支持多字符，如 `"::"`；`"\t"`/`"tab"` 表示制表符） |
| `date_format` | 时间列解析格式**全局默认**（默认 `"epoch_ms"`；可选 `epoch_s`/`epoch_us`/`epoch_ns`/`iso`） |
| `undeclared_files` | 数据目录里有未声明 CSV 时的行为：`"error"`（默认，报错并列出文件）或 `"ignore"` |
| `ignore` | 属于本数据集但不参与初始装载的文件（如 LDBC 的 `updateStream_*.csv`） |

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
  "labels": {
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

## 与 neo4j-admin import 的对照

`neo4j-admin import` 的 `--nodes` / `--relationships` 在这里的等价物就是 schema 的
`labels` / `relationships`；`headers.txt` 的列类型声明对应 `columns`；
`:LABEL` 列对应 `label`；`--delimiter` 对应顶层 `delimiter`。

sf0.1 语料的现成配置：[ldbc-sf01.schema.json](../design/ldbc-sf01.schema.json)
（数据一致性可用 `scripts/verify_loader_vs_neo4j.py` 核对，判据取自
`ldbc_snb_interactive_v1_impls/cypher/scripts`）。
