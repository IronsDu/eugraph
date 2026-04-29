# KV 编码设计

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

源码：`src/storage/kv/key_codec.hpp`、`src/storage/kv/index_key_codec.hpp`

---

## 架构概述

采用 **WiredTiger 多表隔离** 设计。每个逻辑数据结构有独立的 WT 表，表名即身份标识，key 中无前缀字节。所有多字节整数使用大端序（Big-Endian）编码。

### 表清单

| WT 表名 | 用途 | Key 格式 |
|---------|------|----------|
| `label_reverse` | 顶点→标签反向索引 | `{vertex_id:u64BE}{label_id:u16BE}` |
| `label_fwd_{id}` | 标签→顶点正向索引（每标签一表） | `{vertex_id:u64BE}` |
| `pk_forward` | 主键→顶点 ID | `{pk_value:raw}` |
| `pk_reverse` | 顶点 ID→主键 | `{vertex_id:u64BE}` |
| `vprop_{id}` | 顶点属性存储（每标签一表） | `{vertex_id:u64BE}{prop_id:u16BE}` |
| `edge_index` | 边邻接索引 | `{vertex_id:u64BE}{direction:u8}{label_id:u16BE}{neighbor_id:u64BE}{seq:u64BE}` |
| `etype_{id}` | 边类型索引（每关系类型一表） | `{src_id:u64BE}{dst_id:u64BE}{seq:u64BE}` |
| `eprop_{id}` | 边属性存储（每关系类型一表） | `{edge_id:u64BE}{prop_id:u16BE}` |
| `vidx_{label_id}_{prop_id}` | 顶点二级索引 | `{sortable_value}{entity_id:u64BE}` |
| `eidx_{label_id}_{prop_id}` | 边二级索引 | `{sortable_value}{entity_id:u64BE}` |

---

## 详细编码

### 标签反向索引（label_reverse）

记录顶点拥有哪些标签。每顶点-标签组合一条记录。

```
查询：前缀扫描 {vertex_id:u64BE} → 获取该顶点所有标签
```

### 标签正向索引（label_fwd_{id}）

每个标签一张表，表名含 label_id。按顶点 ID 排序。

```
查询：全表扫描 → 获取该标签下所有顶点 ID
```

### 主键索引（pk_forward / pk_reverse）

全局主键，每顶点一个。pk_forward 用于 pk→vid 查找，pk_reverse 用于 vid→pk 反查。

### 顶点属性（vprop_{id}）

每个标签一张表。prop_id 在每个 Label 内独立编号、永不复用。字段改名只需修改元数据中的 PropertyDef，无需修改 KV 数据。

```
前缀 {vertex_id:u64BE} → 获取某顶点在该标签下的所有属性
前缀 {vertex_id:u64BE}{prop_id:u16BE} → 获取某顶点在该标签下的指定属性
```

### 边邻接索引（edge_index）

全局一张表。每条逻辑边产生**两条**索引记录（出边 + 入边）：一条 `direction=OUT`，一条 `direction=IN`，指向同一个 edge_id。

```
前缀 {vertex_id}{OUT} → 所有出边
前缀 {vertex_id}{OUT}{label_id} → 某类型出边
前缀 {vertex_id}{OUT}{label_id}{neighbor_id} → 两点间某类型所有边
```

### 边类型索引（etype_{id}）

每个关系类型一张表。仅存出边方向，每条逻辑边一条记录。用于按关系类型扫描所有边。

### 边属性（eprop_{id}）

每个关系类型一张表。与 vprop 设计一致，prop_id 在 EdgeLabel 内编号。

---

## 二级索引（IndexKeyCodec）

`IndexKeyCodec` 实现可排序的属性值编码，key 格式为 `{sortable_value}{entity_id:u64BE}`：

| 属性类型 | 编码格式 |
|----------|----------|
| null | `0xFF`（排最前） |
| bool | `0x00` / `0x01` |
| int64 | `0x00` + 8 字节 BE，符号翻转（`val ^ 0x8000...`） |
| double | `0x01` + 8 字节 IEEE 754，符号翻转 |
| string | `0x02` + 原始字节（v1 仅 ASCII 排序） |

等值查询使用 `encodeEqualityPrefix` 做前缀扫描；范围查询用 `search_near` + 边界检查。

---

## 元数据编码

元数据存储使用独立的 WT 连接（`{db}/meta/`），表内 key 使用 `M|` 前缀字符串：

```
M|label:{name}       → {label_id:u16}{properties_def:binary}{indexes:binary}
M|edge_label:{name}  → {edge_label_id:u16}{properties_def:binary}{directed:u8}{indexes:binary}
M|label_id:{id}      → {name}{properties_def}{indexes}
M|edge_label_id:{id} → {name}{properties_def}{directed}{indexes}
M|index:{name}       → {label_id:u16}{prop_id:u16}{is_edge:u8}{state:u8}
M|next_ids           → {next_vertex_id:u64}{next_edge_id:u64}{next_label_id:u16}{next_edge_label_id:u16}
```

Values 使用 `MetadataCodec` 进行二进制编解码（`src/storage/meta/meta_codec.cpp`），非 ASCII 管道分隔。

---

## 关键设计决策

1. **多表隔离替代单 KV 前缀**：每个 Label/EdgeLabel 拥有独立的 WT 表，表名即分区依据，避免单表前缀扫描时的跨 label 干扰，并能独立配置 WT 表选项
2. **prop_id 替代属性名**：KV key 中存 prop_id 而非属性名字符串，字段改名只需修改元数据，无需重写数据
3. **prop_id 永不复用**：删除列后 prop_id 保留空洞，新增列分配新 ID，避免旧数据被误读
4. **边双向索引**：edge_index 表同时维护出边和入边索引，每条逻辑边对应两条记录
