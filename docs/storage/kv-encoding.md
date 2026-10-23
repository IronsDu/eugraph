# KV 编码设计

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

源码：`src/storage/kv/key_codec.hpp`、`src/storage/kv/adjacency_codec.hpp`、`src/storage/kv/index_key_codec.hpp`

## 架构概述

采用 **WiredTiger 多表隔离** 设计。每个逻辑数据结构有独立的 WT 表，表名即身份标识，key 中无前缀字节。所有多字节 key 使用大端序；`vertex_data` 的邻接 blob value 使用小端序。

### 表清单

| WT 表名 | 用途 | Key 格式 |
|---------|------|----------|
| `vertex_data` | 顶点标签和有界出边缓存 | `{vertex_id:u64BE}{kind:u8}{...}` |
| `vprop_{id}` | 顶点属性（每标签一表） | `{vertex_id:u64BE}{prop_id:u16BE}` |
| `vertex_existence` | 全图顶点枚举（含无标签顶点） | `{vertex_id:u64BE}` |
| `label_fwd_{id}` | 标签→顶点正向索引（每标签一表） | `{vertex_id:u64BE}` |
| `edge_index` | 边邻接索引 | `{vertex_id:u64BE}{direction:u8}{label_id:u16BE}{neighbor_id:u64BE}{seq:u64BE}` |
| `etype_{id}` | 边类型索引（每关系类型一表） | `{src_id:u64BE}{dst_id:u64BE}{seq:u64BE}` |
| `eprop_{id}` | 边属性存储（每关系类型一表） | `{edge_id:u64BE}{prop_id:u16BE}` |
| `vidx_{index_id}` | 顶点二级索引 | `{sortable_value}{entity_id:u64BE}` |
| `eidx_{index_id}` | 边属性索引 | `{sortable_value}{edge_id:u64BE}` |

`label_reverse` 已删除；标签进 `vertex_data`。属性最终保留在 `vprop_{id}`：纯 X 合并属性会让 LDBC 的单属性点查 B-tree 变大（complex-3 回退约 35%），因此采用 hybrid。

---

## 顶点数据（vertex_data）

```text
{v:u64BE}{kind:u8}{...}
```

| kind | 含义 | Key | Value |
|------|------|-----|-------|
| `0x00` | 标签 | `{v}{0x00}{label_id:u16BE}` | 空 |
| `0x01` | 属性（旧纯 X 数据兼容/保留编码） | `{v}{0x01}{label_id:u16BE}{prop_id:u16BE}` | `ValueCodec` |
| `0x02` | 出边缓存 | `{v}{0x02}` | 邻接 blob，见下 |

查询前缀：

```text
{v}                       -> 该顶点所有标签/邻接
{v}{0x00}                 -> 所有标签
{v}{0x00}{label}          -> 某标签是否存在
{v}{0x02}                 -> 出边缓存
```

同一顶点的标签/邻接记录在 `vertex_data` 中连续；属性在 `vprop_{id}` 中按标签分区。这样既保留标签/邻接的局部性，又避免属性点查落入一棵包含所有标签记录的大 B-tree。

### 顶点属性（vprop_{id}）

每个标签一张表。`prop_id` 在每个 Label 内独立编号、永不复用。

```
前缀 {vertex_id:u64BE}                  -> 该顶点在该标签下的所有属性
前缀 {vertex_id:u64BE}{prop_id:u16BE}   -> 指定属性
```

保留 per-label 表是性能决策：纯 `vertex_data` 属性合并后，Message/Person 等热标签的单属性点查
需要落到约 69MB 的全局 B-tree，而旧的 `vprop_Message`/`vprop_Person` 分别约 28/29MB，页局部性
明显更好；sf0.1 complex-3 实测纯 X 回退约 35%。

### 标签/属性 DDL

- `createLabel` 创建 `label_fwd_{id}` 和 `vprop_{id}`；`vertex_data` 是全局表。
- `dropLabel`：删除 `label_fwd_{id}` 枚举出的顶点的 X 标签 key，再 drop 正向表与 `vprop_{id}`；属性表的 drop 是 O(1)。
- 删除顶点：扫 `{v}` 前缀删除标签/邻接，删除各标签的 `vprop` 属性，再删 `label_fwd` 和 `vertex_existence`。

---

## 出边缓存 blob

### 编码

```text
value = {adj_state:u8}
        repeated count 次，固定 32 字节 entry（小端）：
          offset 0:  edge_label:u16
          offset 2:  reserved0:u16   (必须为 0)
          offset 4:  reserved1:u32   (必须为 0)
          offset 8:  neighbor_id:u64
          offset 16: seq:u64
          offset 24: edge_id:u64
```

- **没有 count 字段**：`count = (value.size() - 1) / 32`；
- blob 变长，不 padding 到 N；
- `(size - 1) % 32 != 0`、state 非法、reserved 非 0 时视为损坏，调用方回退 `edge_index`；
- entry 固定 32B，便于删除时 `memmove` 压缩，也便于拷贝到对齐的运行时结构后直接读字段。

### 状态

```text
COMPLETE = 0
OVERFLOW = 1
```

- 创建顶点时若 cache 开启，写一个空 blob（`COMPLETE, count=0`），用于区分“缓存完整且出度为 0”和“没有缓存”；
- 插入时永远写 `edge_index`；`COMPLETE && count < N` 追加 entry；`COMPLETE && count == N` 时插入第 N+1 条置 `OVERFLOW`，之后不再缓存；
- 删除时永远删 `edge_index`；blob 中有该 edge 就删除并把后面的 entry 前移；状态保持（`OVERFLOW` 不回退）；
- entry 按 `(edge_label, neighbor_id, seq, edge_id)` 排序；
- 第一版只缓存 **OUT**；`IN/BOTH` 仍走 `edge_index`。

### 配置

`--adj-cache-capacity N`（0 关闭，默认 0）。`N=0` 时不创建/维护 blob。当前版本默认关闭；cache 的查询侧接入需要显式传入 `OutEdgeCache*`。

### 读取接口

```cpp
VertexData loadVertexData(GraphSession&, VertexId, uint32_t needs);
void scanEdges(GraphSession&, VertexId, const OutEdgeCache* cache,
               Direction, std::optional<EdgeLabelId>,
               const std::function<bool(const EdgeIndexEntry&)>& callback);
```

- `cache == nullptr` 或 `OVERFLOW` → `edge_index`；
- `OUT && COMPLETE` → 直接过滤 blob；
- `IN/BOTH` → `edge_index`；
- 不允许通用 `scanEdges(vid, OUT)` 隐式读 blob：只有调用方明确持有 cache 时才走缓存，避免无收益的额外 B-tree 读取。

> 读放大/写热点：cache blob 是每顶点单条记录，高并发写同一顶点可能产生事务冲突；当前默认关闭，开启前需要评估批量装载/并发更新场景。

---

## 边邻接索引（edge_index）

全局一张表。每条逻辑边产生两条索引记录（OUT + IN），指向同一个 edge_id。

```
前缀 {vertex_id}{OUT}                      -> 所有出边
前缀 {vertex_id}{OUT}{label_id}            -> 某类型出边
前缀 {vertex_id}{OUT}{label_id}{neighbor}  -> 两点间某类型所有边
```

### 边类型索引（etype_{id}）

每个关系类型一张表，仅存出边方向，每条逻辑边一条。

### 边属性（eprop_{id}）

每个关系类型一张表，`prop_id` 在 EdgeLabel 内编号。

---

## 二级索引

`IndexKeyCodec` 实现可排序属性值编码，key 为 `{sortable_value}{entity_id:u64BE}`：

| 属性类型 | 编码 |
|----------|------|
| null | `0xFF`（排最前） |
| bool | `0x00` / `0x01` |
| int64 | `0x00` + 8B BE（符号翻转） |
| double | `0x01` + 8B IEEE754（符号翻转） |
| string | `0x02` + 原始字节（v1 仅 ASCII 排序） |

边属性索引 value 存 `{src_id:u64BE}{dst_id:u64BE}{seq:u64BE}{label_id:u16BE}`，`EdgeIndexScanPhysicalOp` 可直接产出邻接信息。

---

## 元数据编码

元数据使用独立 WT 连接（`{db}/meta/`），`M|` 前缀字符串键：

```text
M|label:{name}       -> {label_id:u16}{properties_def:binary}{indexes:binary}
M|edge_label:{name}  -> {edge_label_id:u16}{properties_def:binary}{directed:u8}{indexes:binary}
M|label_id:{id}      -> {name}{properties_def}{indexes}
M|edge_label_id:{id} -> {name}{properties_def}{directed}{indexes}
M|index:{name}       -> {label_id:u16}{prop_id:u16}{is_edge:u8}{state:u8}
M|next_ids           -> {next_vertex_id:u64}{next_edge_id:u64}{next_label_id:u16}{next_edge_label_id:u16}
```

---

## 兼容与迁移

- 这是 KV 格式变更：旧的 `label_reverse` 需要迁移到 `vertex_data` 的标签记录；`vprop_*` 可保留不动（hybrid 直接复用）。
- 旧的“纯 X 属性”数据不再是当前读取格式，需要重新 load/迁移到 `vprop_*`。
- 新二进制会拒绝仍带 `label_reverse` 的目录；需先用迁移工具把标签写入 `vertex_data` 并删除 `label_reverse`。
- 未引入 per-record version；格式版本在表/图级别管理。
- 第一版 cache 默认关闭；开启后旧顶点如果没有 blob，统一回退 `edge_index`，不会被误认为“空且完整”。
