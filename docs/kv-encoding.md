# KV 编码设计

> 参见 [overview.md](overview.md) 返回文档导航

## Key 前缀定义

```
前缀分配 (单字节):
┌──────────┬────────────────────────────────────┐
│ 前缀     │ 用途                               │
├──────────┼────────────────────────────────────┤
│ 'L' 0x4C │ Label 反向索引 (Vertex → Labels)   │
│ 'I' 0x49 │ Label 正向索引 (Label → Vertex)    │
│ 'P' 0x50 │ 主键正向索引 (pk → vertex_id)      │
│ 'R' 0x52 │ 主键反向索引 (vertex_id → pk)       │
│ 'X' 0x58 │ 属性存储 (Property Storage)        │
│ 'E' 0x45 │ Edge 索引 (邻接查询)               │
│ 'G' 0x47 │ Edge 类型索引 (按关系类型扫描)       │
│ 'D' 0x44 │ Edge 属性存储 (Property Storage)    │
│ 'M' 0x4D │ 元数据 (Metadata)                  │
│ 'T' 0x54 │ 事务 (Transaction)                 │
│ 'W' 0x57 │ WAL                                │
└──────────┴────────────────────────────────────┘
```

## 详细编码

### 标签反向索引 (Vertex → Labels)

```
Key:   L|{vertex_id:uint64 BE}|{label_id:uint16 BE}
Value: (empty)

说明: 记录顶点拥有的标签，每个 vertex-label 组合一条记录

示例:
  L|0x0000000000000001|0x0001 → (empty)  // Alice 是 Person
  L|0x0000000000000001|0x0002 → (empty)  // Alice 是 User
  L|0x0000000000000002|0x0001 → (empty)  // Bob 是 Person

前缀查询:
  L|{vertex_id}|  → 获取顶点的所有标签
```

### 标签正向索引 (Label → Vertices)

```
Key:   I|{label_id:uint16 BE}|{vertex_id:uint64 BE}
Value: (empty)

说明: 用于高效查询某标签下的所有顶点

示例:
  I|0x0001|0x0000000000000001 → (empty)  // Alice 是 Person
  I|0x0001|0x0000000000000002 → (empty)  // Bob 是 Person
  I|0x0002|0x0000000000000001 → (empty)  // Alice 是 User

前缀查询:
  I|{label_id}|  → 获取该标签下的所有顶点 ID
```

### 主键正向索引 (pk → vertex_id)

```
Key:   P|{pk_value:encoded}
Value: {vertex_id:uint64 BE}

说明:
  - 全局主键，每个顶点只有一个主键值，用于唯一标识该顶点
  - pk_value 在全局范围内唯一
  - 主键是顶点的全局标识，不属于任何特定标签

示例:
  P|"alice@example.com" → 0x0000000000000001
  P|"bob@example.com"   → 0x0000000000000002
```

### 主键反向索引 (vertex_id → pk)

```
Key:   R|{vertex_id:uint64 BE}
Value: {pk_value:encoded}

说明:
  - 与 P| 互为反向索引，通过 vertex_id 反查全局主键值
  - 每个顶点一条记录

示例:
  R|0x0000000000000001 → "alice@example.com"
  R|0x0000000000000002 → "bob@example.com"
```

### 属性存储

```
Key:   X|{label_id:uint16 BE}|{vertex_id:uint64 BE}|{prop_id:uint16 BE}
Value: {encoded_value}

说明:
  - prop_id 在每个 Label 内独立编号
  - Key 中使用 prop_id 而非属性名，字段改名只需修改元数据，无需修改数据存储

  - 支持部分属性查询

  - 同一 label 下的不同顶点可以使用相同的 prop_id（但字段可能不同）

  - 同一顶点可以有多个标签，因此可能有多组属性存储记录

    - X 格式： X|{label_id}|{vertex_id}|{prop_id}， prop_id 在各自 Label 内独立编号

    - 每组属性一条记录，便于部分查询

    - 支持字段改名（只改元数据）

  - 与 Edge 数据共享相同的 prop_id 空间

示例:
  X|0x0001|0x0000000000000001|0x0001 → "Alice"   // Person(id=1) 的顶点1 的属性1 (name)
  X|0x0001|0x0000000000000001|0x0002 → 30        // Person(id=1) 的顶点1 的属性2 (age)
  X|0x0001|0x0000000000000002|0x0001 → "Beijing"   // Person(id=1) 的顶点2 的属性1 (city)

  // 同一顶点1 也可以属于另一个标签，因此有两条属性存储记录
  X|0x0002|0x0000000000000002|0x0001 → "Bob"     // Company(id=2) 的顶点2 的属性1 (name)

  X|0x0002|0x0000000000000002|0x0002 → "Beijing"   // 公司(id=2) 的顶点2 的属性2 (city)

前缀查询:
  X|{label_id}|{vertex_id}|           → 获取某标签下某顶点的所有属性
  X|{label_id}|{vertex_id}|{prop_id}   → 获取某标签下某顶点的指定属性
```

### Edge 索引 (邻接查询)

```
Key:   E|{vertex_id:uint64 BE}|{direction:uint8}|{edge_label_id:uint16 BE}|{neighbor_id:uint64 BE}|{附加id:uint64 BE}
Value: {edge_id:uint64 BE}

方向编码:
  OUT = 0x00
  IN  = 0x01

说明:
  - 附加id 用于区分两点间同类型的多条边（由业务层决定）
  - edge_id 存储在 value 中，用于获取边的完整信息
  - 一条逻辑边（如 A follows B）会产生两条 E| 索引记录：
    - E|A|OUT|follows|B|{seq} → edge_id  （出边索引，A 为起点）
    - E|B|IN|follows|A|{seq} → edge_id   （入边索引，B 为终点）
  - 两条索引指向同一个 edge_id，共享同一份属性存储（D|）
  - 插入/删除边时需同时维护这两条索引记录

示例:
  // 公司A(id=1) 向 公司B(id=2) 开了3张发票
  E|0x0000000000000001|0x00|0x0001|0x0000000000000002|0x0000000000000001 → 100  // 第1张发票
  E|0x0000000000000001|0x00|0x0001|0x0000000000000002|0x0000000000000002 → 101  // 第2张发票
  E|0x0000000000000001|0x00|0x0001|0x0000000000000002|0x0000000000000003 → 102  // 第3张发票

前缀查询能力:
  E|{vertex_id}|OUT                              → 某点所有出边
  E|{vertex_id}|IN                               → 某点所有入边
  E|{vertex_id}|OUT|{label_id}                   → 某点某类型出边
  E|{vertex_id}|OUT|{label_id}|{neighbor_id}     → 某点到某点的某类型所有边
  E|{vertex_id}|OUT|{label_id}|{neighbor_id}|{附加id} → 某点到某点的某类型特定边
```

### Edge 类型索引 (按关系类型扫描)

```
Key:   G|{edge_label_id:uint16 BE}|{src_vertex_id:uint64 BE}|{dst_vertex_id:uint64 BE}|{附加id:uint64 BE}
Value: {edge_id:uint64 BE}

说明:
  - 以 edge_label_id 为前缀，支持按关系类型扫描所有边
  - 仅存储出边方向，每条逻辑边只产生一条 G| 记录（与 E| 的双向索引不同）
  - 与 E| 索引的定位区分：
    - E| = 邻接查询（从某个顶点出发扫描边）
    - G| = 类型扫描（从某种关系类型出发扫描所有边）
  - 附加id 含义与 E| 一致，用于区分两点间同类型的多条边

示例:
  // edge_label_id=1 (follows), Alice(1) follows Bob(2), 附加id=0
  G|0x0001|0x0000000000000001|0x0000000000000002|0x0000000000000000 → 100

  // edge_label_id=1 (follows), Alice(1) follows Charlie(3), 附加id=0
  G|0x0001|0x0000000000000001|0x0000000000000003|0x0000000000000000 → 101

  // edge_label_id=2 (likes), Bob(2) likes Product(4), 附加id=0
  G|0x0002|0x0000000000000002|0x0000000000000004|0x0000000000000000 → 200

前缀查询能力:
  G|{edge_label_id}                                        → 扫描某关系类型的所有边
  G|{edge_label_id}|{src_id}                               → 某关系类型下某起点发出的所有边
  G|{edge_label_id}|{src_id}|{dst_id}                      → 某关系类型下两点间的所有边
  G|{edge_label_id}|{src_id}|{dst_id}|{附加id}              → 某关系类型下两点间的特定边
```

### Edge 属性存储

```
Key:   D|{edge_label_id:uint16 BE}|{edge_id:uint64 BE}|{prop_id:uint16 BE}
Value: {encoded_value}

说明:
  - prop_id 在每个 EdgeLabel 内独立编号，与 Vertex 属性存储 (X|) 设计一致
  - Key 中使用 prop_id，字段改名只需修改元数据，无需修改数据存储
  - 支持部分属性查询，无需读取所有属性
  - 仅存储边的属性，src_id/dst_id/edge_label_id/direction 可从边索引查询中获取
  - 一条逻辑边（如 A follows B）会产生两条索引记录：
    - E|A|OUT|follows|B|{seq} → edge_id
    - E|B|IN|follows|A|{seq} → edge_id
  - 两条索引指向同一个 edge_id，共享同一份属性存储

示例:
  // edge_label_id=1 (follows), edge_id=100 的边
  D|0x0001|0x0000000000000064|0x0001 → 2020     // follows 的属性1 (since)
  D|0x0001|0x0000000000000064|0x0002 → 0.8       // follows 的属性2 (weight)

前缀查询:
  D|{edge_label_id}|{edge_id}|           → 获取某条边的所有属性
  D|{edge_label_id}|{edge_id}|{prop_id}   → 获取某条边的指定属性
```

### 元数据

```
Key:   M|label:{name}
Value: {label_id:uint16}|{properties_def}|{primary_key}|{indexes}

Key:   M|label_id:{id:uint16}
Value: {name}|{properties_def}|{primary_key}|{indexes}

Key:   M|edge_label:{name}
Value: {edge_label_id:uint16}|{properties_def}|{directed:bool}

Key:   M|edge_label_id:{id:uint16}
Value: {name}|{properties_def}|{directed:bool}

Key:   M|next_ids
Value: {next_vertex_id:uint64}|{next_edge_id:uint64}|{next_label_id:uint16}|{next_edge_label_id:uint16}
```

## 查询能力总结

```
┌────────────────────────────────────────────────────────────────────────────────┐
│                              查询能力                                          │
├────────────────────────────────────────────────────────────────────────────────┤
│                                                                                │
│  ────────────────── Vertex 查询 ──────────────────                            │
│                                                                                │
│  • 通过 vertex_id 获取属性          X|{label_id}|{vertex_id}|*  (前缀扫描)       │
│  • 通过 vertex_id 获取标签          L|{vertex_id}|*  (前缀扫描)              │
│  • 通过 label_id 扫描顶点          I|{label_id}|*  (前缀扫描)              │
│  • 通过主键获取 vertex_id          P|{pk_value}                               │
│  • 通过 vertex_id 获取主键          R|{vertex_id}                               │
│  • 通过属性值扫描顶点              (待设计二级索引)                           │

│                                                                                │
│  ────────────────── Edge 查询 ──────────────────                              │
│                                                                                │
│  • 查询某点所有出边                  E|{vid}|OUT|*                             │
│  • 查询某点所有入边                  E|{vid}|IN|*                              │
│  • 查询某点某类型出边                E|{vid}|OUT|{label_id}|*                 │
│  • 查询某点某类型入边                E|{vid}|IN|{label_id}|*                  │
│  • 查询两点之间某类型所有边        E|{src}|OUT|{label}|{dst}|*                 │
│                                                                                │
│  • 按关系类型扫描所有边              G|{edge_label_id}|*                         │
│  • 某关系类型下某起点的所有边        G|{edge_label_id}|{src_id}|*                │
│  • 某关系类型下两点间所有边          G|{edge_label_id}|{src_id}|{dst_id}|*       │
│
│  • 通过 edge_id 获取边详情           D|{edge_label_id}|{edge_id}|*  (前缀扫描)   │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
```
