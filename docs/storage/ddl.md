# DDL（数据定义语言）设计

> [当前实现 + 设计规划] 参见 [README.md](../README.md) 返回文档导航

本文档描述 schema 级别的 DDL 操作设计，包括删除标签类型、删除关系类型、列的增删改等。

**当前实现状态**：`ISyncGraphDataStore` 已实现 `dropLabel`/`dropEdgeLabel` 的**物理清理 + 删表**：先通过 `label_fwd`/`etype` 枚举受影响的顶点或边，删除 `vertex_data` / `edge_index` 中对应记录，再 drop per-label 表。handler 层尚未暴露这些操作。列操作（增/删/改）均未实现。

> 编码格式基于当前 WiredTiger 多表架构（`src/storage/kv/key_codec.hpp`），表名常量见 `src/common/types/constants.hpp`。
> 顶点标签/属性/邻接编码见 [kv-encoding.md](kv-encoding.md)。

---

## 一、删除标签类型（Drop Label）

### 1.1 设计目标

- 删除某个标签类型（Label），速度要快
- 删除后，顶点本身保留（顶点可能还有其他标签）
- 该标签下的属性数据和索引数据被清除

### 1.2 数据分析

删除标签类型涉及以下 WT 表：

| WT 表名 | 用途 | 清理方式 |
|----------|------|-------------|
| `table:label_fwd_{id}` | 标签正向索引（Label → Vertices） | ✅ 整表 Drop，O(1) |
| `table:vertex_data` | 标签 + 属性（全局表，`{vid}{0x00|0x01}{label}` 记录） | ❌ 不能整表 Drop；按 `label_fwd` 枚举顶点后删除对应前缀 |
| `table:vertex_existence` | 顶点存在表 | ❌ 不能删；顶点本身保留 |

旧实现的 `label_reverse`/`vprop_{id}` 已移除；标签与属性共享全局 `vertex_data` 表，因此 `DROP LABEL` 不再是纯 O(1) 删表，需要先枚举该标签下的顶点。

### 1.3 当前流程

```
1. 扫描 table:label_fwd_{id}，收集该标签下的所有 vid
2. 对每个 vid：
   - 删除 vertex_data 的 {vid}{0x00}{label_id}（标签记录）
   - 扫描并删除 vertex_data 的 {vid}{0x01}{label_id} 前缀（属性记录）
3. WT drop table:label_fwd_{id}
4. 元数据侧 tombstone 字段在 GraphSchema 中预留；handler 尚未暴露 DROP，当前没有写入/过滤路径
```

#### 读取时的处理

- **查某标签下的顶点**（scan label_fwd）→ 表已删除，查不到 ✅
- **查某顶点的标签**（`vertex_data` kind 0x00）→ 标签记录已删除，不会返回该 label ✅
- **查某顶点在某标签下的属性**（`vertex_data` kind 0x01 前缀）→ 属性记录已删除，查不到 ✅
- **同顶点的其他标签** → 不受影响，`vertex_existence` 保留 ✅

### 1.4 元数据变更

Label 定义中增加状态字段：

```
M|label:{name}
Value: {label_id:u16BE}{status:u8}{properties_def}{indexes}

  status:
    0x00 = 正常 (ACTIVE)
    0x01 = 已删除 (DELETED)
```

删除标签类型时，将 status 改为 DELETED 而非删除整条记录，保留 tombstone 信息。

### 1.5 实现状态

- `ISyncGraphDataStore::dropLabel(LabelId)` — **已实现**（枚举 `label_fwd_{id}`，删除 `vertex_data` 标签/属性记录，再 drop `label_fwd_{id}`）
- `GraphSchema::deleted_labels` 字段 — **已预留，未接线**（无 DROP 入口，也无读取过滤）
- Handler 层暴露 DROP LABEL — **未实现**
- `label_reverse` 清理 — **N/A**（表已移除）

---

## 二、删除关系类型（Drop EdgeLabel）

### 2.1 设计目标

- 删除某个关系类型（EdgeLabel），速度要快
- 需同时删除该关系类型的所有边属性
- 顶点不受影响

### 2.2 数据分析

| WT 表名 | 用途 | 清理方式 |
|----------|------|-------------|
| `table:etype_{id}` | 边类型索引（按 src→dst 排序） | ✅ 整表 Drop，O(1) |
| `table:eprop_{id}` | 边属性存储 | ✅ 整表 Drop，O(1) |
| `table:edge_index` | 全局邻接索引，key 含 label_id | ❌ label_id 在 key 中间；按 etype 枚举边后逐条删除 |
| `table:vertex_data` | 可选邻接缓存（N>0 时的 kind 0x02/0x03） | ❌ 按端点 state 删除缓存 key |

### 2.3 当前流程

```
1. 扫描 table:etype_{id}，收集该 label 的所有 (src, dst, seq)
2. 对每条边：
   - 对 src/OUT 与 dst/IN 两个方向分别读 vertex_data 的 state：
     - COMPLETE：删除 {vid}{kind}{label}{neighbor}{seq} 缓存 key，count--
     - OVERFLOW / 无 state：删除 edge_index 的对应 key
3. WT drop table:etype_{id}
4. WT drop table:eprop_{id}
5. 元数据侧 tombstone 字段在 GraphSchema 中预留；handler 尚未暴露 DROP，当前没有写入/过滤路径
```

#### 读取时的处理

- **按关系类型扫描边**（scan etype）→ 表已删除，查不到 ✅
- **从某顶点遍历边**（scan edge_index / vertex_data 缓存）→ 对应邻接条目已删除，查不到 ✅
- **查某条边的属性**（scan eprop）→ 表已删除，查不到 ✅
- **统计度数**（countDegree）→ 只统计剩余邻接条目 ✅

### 2.4 实现状态

- `ISyncGraphDataStore::dropEdgeLabel(EdgeLabelId)` — **已实现**（枚举 `etype_{id}`，清理 `edge_index` / `vertex_data` 缓存，再 drop `etype_{id}` + `eprop_{id}`）
- `GraphSchema::deleted_edge_labels` 字段 — **已预留，未接线**（无 DROP 入口，也无读取过滤）
- Handler 层暴露 DROP EDGE_LABEL — **未实现**
- `edge_index` 清理 — **已实现**

---

## 三、列操作（Column DDL）

> 删除列、重命名列、添加列均基于 prop_id 机制，属于**纯元数据操作**，不涉及 KV 数据的扫描或修改。**当前均未实现。**

### 3.1 设计前提：prop_id 机制

当前 KV 编码中，属性存储使用 prop_id 而非属性名：

```
Vertex 属性:  table:vertex_data  key: {vertex_id:u64BE}{0x01}{label_id:u16BE}{prop_id:u16BE}
Edge 属性:    table:eprop_{id}  key: {edge_id:u64BE}{prop_id:u16BE}
```

prop_id 在每个 Label/EdgeLabel 内单调递增、**永不复用**。属性名与 prop_id 的映射关系保存在元数据 LabelDef/EdgeLabelDef 中。

### 3.2 删除列（Drop Column）

从 Label/EdgeLabel 的 `PropertyDef` 列表中移除目标 prop_id 的定义，写回元数据即生效。

- 已存储的 KV 数据保留在表中成为孤儿数据
- 读取时只返回 PropertyDef 中定义的 prop_id，已删除的 prop_id 不会出现在结果中
- prop_id 永不复用，添加新列不会误读旧数据

孤儿数据可通过后台 GC 任务定期扫描清理。

### 3.3 重命名列（Rename Column）

修改 PropertyDef 中目标 prop_id 的名称，写回元数据即生效。**无需修改任何 KV 数据**（KV 中只存 prop_id，不存属性名）。

### 3.4 添加列（Add Column）

在 PropertyDef 中新增一个 prop_id 定义，分配新的 prop_id（递增），写回元数据。添加列时需满足以下条件之一以避免回填数据：

- **指定默认值**：读取时，若 KV 中不存在该 prop_id 的数据，返回默认值
- **允许为空**（nullable）：读取时，若 KV 中不存在该 prop_id 的数据，返回 null

```cpp
struct PropertyDef {
    uint16_t id;
    string name;
    PropertyType type;
    bool required = false;
    optional<PropertyValue> default_value;  // 默认值
};
```

约束: `default_value.has_value() || !required`（有默认值或非 required）
