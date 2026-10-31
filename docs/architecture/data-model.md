# 数据模型

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

---

## 一、核心概念

### Vertex（顶点）

- `id: uint64` — 系统生成的全局唯一 ID
- 通过 `LabelIdSet` 持有多个标签（Person, User 等）
- 属性按标签独立存储（`vertex_data` 中 `{vid}{0x01}{label_id}{prop_id}` 记录），按 prop_id 索引
- 标签集合存放在 `vertex_data` 的 `{vid}{0x00}{label_id}` 记录，`label_fwd_{id}` 负责按标签反向扫描

### Edge（边）

- `id: uint64` — 系统生成的全局唯一 ID
- `src_id / dst_id: uint64` — 源/目标顶点 ID
- `label_id: uint16` — 边类型（KNOWS, LIVES_IN 等）
- 方向：OUT/IN/BOTH，每条逻辑边在 `edge_index` 表中对应 OUT+IN 两条记录（默认 N=0 时全量；邻接缓存 COMPLETE 时记录在 `vertex_data`，`edge_index` 只存 OVERFLOW 方向）
- 属性按边类型独立存储（每边类型一个 `eprop_{id}` 表）

---

## 二、Label / EdgeLabel 映射

```
"Person"  → LabelId=1        "KNOWS"     → EdgeLabelId=1
"City"    → LabelId=2        "LIVES_IN"  → EdgeLabelId=2
```

双向映射通过 `GraphSchema` 内存对象维护，元数据持久化在 `M|label:{name}` 和 `M|label_id:{id}` KV 中。

---

## 三、属性模型

- 属性按 Label/EdgeLabel 内的 `prop_id`（uint16）索引
- prop_id 在每个 Label/EdgeLabel 内独立编号、从 1 开始、永不复用
- 字段改名只需修改元数据 `PropertyDef.name`，无需修改 KV 数据（KV 中存 prop_id 而非属性名）
- 删除列后 prop_id 保留空洞，新增列分配新 ID，避免旧数据被误读
- 读取时只返回 `PropertyDef` 中定义的 prop_id，孤儿数据不可见
- **多标签顶点的共享属性**：同一条 CREATE 里多个 label 都声明同名属性时，属性值会写入每个声明它的
  label 的 `vertex_data` 属性记录（`CREATE (p:Post:Message {id: ...})` 两边都有 `id`）。读取 `n.prop` 时
  `LoadVertexPropCoalesce` 对**相同值去重**，所以仍返回标量；不同值才按多标签语义返回 list。
  `n::Label.prop` 仍读该 label 自己的值。
- **Loader 行级标签是例外**：行级标签（如 `City`）只是追加可见性，属性始终写在 schema 主标签
  （如 `Place`）下。因此 `n::Place.name` 有值，而 `n::City.name` 为 null；`n.name` 因跨标签
  便捷查找仍有值。行级标签的 `DESCRIBE LABEL` 会显示主标签同一份字段声明，但不代表 City 下
  已经写入了数据。

---

## 四、二级索引

- 支持顶点属性和边属性的 B-tree 二级索引
- 每个索引有独立的状态机：WRITE_ONLY → PUBLIC / DELETE_ONLY
- 等值查询和范围查询
- 唯一索引（UNIQUE 约束）
- 详细见 [index_design.md](../storage/index_design.md)

---

## 五、运行时类型

查询执行中使用的值类型：

```cpp
Value = variant<monostate, bool, int64_t, double, string,
                VertexValue, EdgeValue, ListValue>
Row = vector<Value>         // 位置式行（列索引对应 Schema）
Schema = vector<string>     // 列名列表
```

`Row` 仅用于"手工构造的小结果集"（索引 DDL / EXPLAIN / 库级 DDL），由
`wrapRowsToChunkGenerator` 一次性转成 `DataChunk` 交给统一管道；算子之间的传输单位是
`DataChunk`（1024 行/批），不再有 `RowBatch`。

详细见 [type-definitions.md](type-definitions.md)。
