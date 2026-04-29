# 数据模型

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

---

## 一、核心概念

### Vertex（顶点）

- `id: uint64` — 系统生成的全局唯一 ID
- 通过 `LabelIdSet` 持有多个标签（Person, User 等）
- 属性按标签独立存储（每标签一个 `vprop_{id}` 表），按 prop_id 索引
- 主键（用户定义）：全局唯一标识（如 email），通过 `pk_forward`/`pk_reverse` 索引

### Edge（边）

- `id: uint64` — 系统生成的全局唯一 ID
- `src_id / dst_id: uint64` — 源/目标顶点 ID
- `label_id: uint16` — 边类型（KNOWS, LIVES_IN 等）
- 方向：OUT/IN/BOTH，每条逻辑边在 `edge_index` 表中对应 OUT+IN 两条记录
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

---

## 四、主键

- 每顶点一个全局唯一主键值，不属于任何标签
- `pk_forward`：pk_value → vertex_id
- `pk_reverse`：vertex_id → pk_value
- 主键可选（插入时可不指定）

---

## 五、二级索引

- 支持顶点属性和边属性的 B-tree 二级索引
- 每个索引有独立的状态机：WRITE_ONLY → PUBLIC / DELETE_ONLY
- 等值查询和范围查询
- 唯一索引（UNIQUE 约束）
- 详细见 [index_design.md](index_design.md)

---

## 六、运行时类型

查询执行中使用的值类型：

```cpp
Value = variant<monostate, bool, int64_t, double, string,
                VertexValue, EdgeValue, ListValue>
Row = vector<Value>         // 位置式行（列索引对应 Schema）
Schema = vector<string>     // 列名列表
RowBatch { CAPACITY = 1024; vector<Row> rows; }
```

详细见 [type-definitions.md](type-definitions.md)。
