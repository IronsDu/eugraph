# KV 编码设计

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

源码：`src/storage/kv/key_codec.hpp`、`src/storage/kv/index_key_codec.hpp`

---

## 架构概述

采用 **WiredTiger 多表隔离** 设计。每个逻辑数据结构有独立的 WT 表，表名即身份标识，key 中无前缀字节。所有多字节整数使用大端序（Big-Endian）编码。

### 表清单

| WT 表名 | 用途 | Key 格式 |
|---------|------|----------|
| `vertex_data` | 顶点标签 + 属性 + 可选邻接缓存（单表，按顶点前缀聚簇） | `{vertex_id:u64BE}{kind:u8}{...}` |
| `vertex_existence` | 顶点存在表（含无标签 vertex，`MATCH (n)` 的全图扫描来源） | `{vertex_id:u64BE}` |
| `label_fwd_{id}` | 标签→顶点正向索引（每标签一表） | `{vertex_id:u64BE}` |
| `edge_index` | 边邻接索引；邻接缓存启用时只保存 OVERFLOW 方向的边 | `{vertex_id:u64BE}{direction:u8}{label_id:u16BE}{neighbor_id:u64BE}{seq:u64BE}` |
| `etype_{id}` | 边类型索引（每关系类型一表） | `{src_id:u64BE}{dst_id:u64BE}{seq:u64BE}` |
| `eprop_{id}` | 边属性存储（每关系类型一表） | `{edge_id:u64BE}{prop_id:u16BE}` |
| `vidx_{index_id}` | 顶点二级索引 | `{sortable_value}{entity_id:u64BE}` |
| `vidx_{index_id}` | 顶点复合索引（单/复合同表名规则） | `{sortable_values...}{entity_id:u64BE}` |
| `eidx_{edge_label_id}_{prop_id...}` | 边属性索引（TODO: 迁移到 eidx_{index_id}） | `{sortable_value}{edge_id:u64BE}` |
| `eidx_{edge_label_id}_{prop_id...}` | 边复合属性索引（TODO: 迁移到 eidx_{index_id}） | `{sortable_values...}{edge_id:u64BE}` |

> **兼容性**：`vertex_data` 取代了旧的 `label_reverse`（vid→labels）与 `vprop_{id}`（每标签顶点属性表）。不做历史数据迁移，已有图需要重新导入。

---

## 详细编码

### 顶点数据（vertex_data）

#### 设计目标与布局

单张 `table:vertex_data`，按顶点前缀聚簇，把标签、属性和可选的邻接缓存放到同一棵 B-tree 中：

```text
{v:u64BE}{kind:u8}{...}

0x00 LABEL      {v}{0x00}{label:u16BE}
0x01 PROPERTY   {v}{0x01}{label:u16BE}{prop:u16BE}

0x02 OUT        {v}{0x02}                              -> state + count
                {v}{0x02}{label:u16BE}{neighbor:u64BE}{seq:u64BE} -> edge_id

0x03 IN         {v}{0x03}                              -> state + count
                {v}{0x03}{label:u16BE}{neighbor:u64BE}{seq:u64BE} -> edge_id
```

| kind | 含义 | Key | Value |
|---|---|---|---|
| `0x00` | 标签 | `{vid:u64BE}{0x00}{label_id:u16BE}` | 空 |
| `0x01` | 属性 | `{vid:u64BE}{0x01}{label_id:u16BE}{prop_id:u16BE}` | ValueCodec |
| `0x02` | OUT 邻接 | `{vid}{0x02}` state；每条边 `{vid}{0x02}{label}{neighbor}{seq}` | state+count；edge_id |
| `0x03` | IN 邻接 | `{vid}{0x03}` state；每条边 `{vid}{0x03}{label}{neighbor}{seq}` | state+count；edge_id |

```
前缀 {vid}{0x00} → 该顶点的所有标签
前缀 {vid}{0x01} → 该顶点的所有标签下属性
前缀 {vid}{0x01}{label} → 某标签下所有属性
精确 {vid}{0x01}{label}{prop} → 某属性
前缀 {vid} → 该顶点所有记录（删除时一次清空）
```

`label_fwd_{id}` 仍保留：按标签扫描、`DROP LABEL` 时枚举该标签顶点。
`label_reverse`、`vprop_{id}` 删除；`edge_index` 在邻接缓存启用时改为 **overflow-only**：只保存 COMPLETE 缓存不足以容纳的顶点的邻接。

#### 顶点属性

prop_id 在每个 Label 内独立编号、永不复用。字段改名只需修改元数据中的 PropertyDef，无需修改 KV 数据。属性按 `{vid}{0x01}{label}{prop}` 存放在 `vertex_data` 中；`DROP LABEL` 通过 `label_fwd` 枚举顶点后删除对应前缀。

#### 邻接缓存（kind 0x02/0x03）

每个顶点的每个方向有一条 state key（`{vid}{kind}`，9 字节），value 为 `{state:u8}{count:u32BE}`：

```cpp
enum class AdjacencyCacheState : uint8_t { COMPLETE = 0, OVERFLOW = 1 };
// value: {state:u8}{count:u32BE}，count 仅 COMPLETE 有意义
```

- 顶点创建：写 OUT/IN 两条 state，COMPLETE + count=0；
- degree ≤ N：边 key 全部在 `vertex_data`，`edge_index` 无该方向条目；
- 第 N+1 条：同一 WT 事务内
  1. 读 state + 该方向全部边 key；
  2. 把 N+1 条边写入 `edge_index`；
  3. 删除 `{v}{0x02|0x03}` 下的边 key（state key 保留）；
  4. state = OVERFLOW；
- OVERFLOW：只读 `edge_index`，不再读 `vertex_data` 的边 key；
- 删除边：COMPLETE 删边 key + count--；OVERFLOW 删 edge_index；
- 不降级；如需回到 COMPLETE，离线重建。

`N` 现在是 `SyncGraphDataStore` 的构造参数，默认 **0**（生产默认关闭缓存）。测试用小的 N 覆盖 COMPLETE/OVERFLOW；后续可以按 label/方向配置。

当前默认 N=0 时：

- `insertVertex` 不写 state/邻接 key；
- `insertEdge` 把两条方向都写进 `edge_index`；
- `scanEdges` / `createEdgeScanCursor` / `countDegree` 全部走 `edge_index`；
- 异步流式 `scanEdges` 使用 caller-owned `out` 缓冲，每批 4096 条，span 只在本批有效。

#### 读写路径

- `scanEdges` / `createEdgeScanCursor`：
  - 读 state；
  - COMPLETE：cursor 从 `{v}{0x02|0x03}` 开始，跳过 state key，顺序扫边 key；
  - OVERFLOW：扫 `edge_index`；
  - `BOTH`：OUT/IN 分别判断后合并（UNDIRECTED Expand 为了保留物理方向，显式拆成 OUT/IN 两次扫描）。
- `countDegree`：COMPLETE 直接用 state.count（O(1)）；OVERFLOW 扫 `edge_index`。
- `scanEdgesBatch`：每 4,096 个 source 复用一个 `edge_index` cursor；缓存启用时回退逐 source 的 `createEdgeScanCursor`。
- 异步 `scanEdges`：调用方传 `std::vector<EdgeIndexEntry>& out`，generator 每批清空并填充，yield `std::span<const EdgeIndexEntry>`；Expand 直接复用成员 `edge_scratch_`。
- `deleteVertex`：`{v}` 前缀删除 label/prop/state/edge key；边由 DETACH DELETE 先删。
- `DROP LABEL`：`label_fwd` 枚举顶点，删 label/属性前缀，再 drop `label_fwd_{id}`。
- `DROP EDGE LABEL`：`etype` 枚举该 label 的边，按端点 state 删除缓存 key 或 `edge_index` 条目，再 drop `etype/eprop`。
- loader：导入时只写 `etype/eprop`，导入后按 degree 一次性构建 `vertex_data` 邻接 + `edge_index` + state，避免逐边溢出迁移（待补）。

#### 查询/算子/优化器利用

1. **PE 的 labels + props**：一次 `{v}{0x00}` / `{v}{0x01}` 前缀扫描，替代旧 `label_reverse` + `vprop` 两次跨表点查；`getVertexCoalescePropertiesBatch` 一次 `{v}{0x01}` 扫描同时完成 labels 判定和候选属性取值。
2. **Expand**：COMPLETE 时直接读 `vertex_data` 邻接 key；低 degree 不再有 `edge_index` 条目，且和顶点属性同表；OVERFLOW 保持 `edge_index` 路径。
3. **countDegree / 基数估计**：COMPLETE 用 state.count（O(1)），CBO 可在不扫描的情况下获得准确 out/in degree，用于 join order、Expand 方向/驱动侧选择。
4. **目标属性下推**：`Expand` 后紧接 `friend IN friends` / `friend.x = ...` 时，可在同一 `vertex_data` 前缀内读取目标属性，减少一次跨表访问。
5. **PC / 半连接**：`CorrelatedVertexProbe` 可按端点直接读 `vertex_data`，缓存 Person 锚点；配合 state 判断是否需要 `edge_index`。
6. **覆盖读**：`{v}{0x01}{label}` 一次读某标签全部属性，适合 `RETURN n` / ConstructVertex；`{v}{0x00}` 读标签集合。
7. **优化器**：把 state/count 作为统计信息（free degree），用于选择 Expand 的驱动侧（高 degree 侧后展开）、估算 Expand 输出行数、判断是否需要 `BatchExpand` / 邻接扫描、PC anchor 选择。

#### 风险与约束

- 首次溢出的原子迁移与并发：同顶点并发插入需串行化/重试；
- OVERFLOW 后 `edge_index` 与 `vertex_data` 的边界必须严格：读路径不能混读；
- `DROP EDGE LABEL`、`deleteVertex`、loader 重建需要同时清理两类记录；
- COMPLETE 路径与 `edge_index` 读法同构，**不会更快**；收益是统一存储、低 degree 无 `edge_index` 条目和写放大；高 degree 仍走 `edge_index`；
- 不做历史迁移，已有图需重新导入。

#### 实施状态

1. `vertex_data` labels/properties + 删除旧 `label_reverse`/`vprop`（已完成）；
2. OUT/IN state + 邻接 key（已完成，默认 N=0 关闭）；
3. `insertEdge`/`deleteEdge` 的分层与溢出迁移（已完成）；
4. `scanEdges` / `createEdgeScanCursor` / `countDegree` state-aware（已完成）；
5. `DELETE` / `DROP EDGE LABEL` 清理（已完成）；loader 后置批量构建（待补）；
6. LDBC 重新导入 + 基准/TCK（已完成）；
7. 查询/优化器接入：PE 融合读取已完成；degree 估计、目标属性下推、PC 直读仍待后续。

### 标签正向索引（label_fwd_{id}）

每个标签一张表，表名含 label_id。按顶点 ID 排序。

```
查询：全表扫描 → 获取该标签下所有顶点 ID
```

### 顶点存在表（vertex_existence）

全局表，key 为 `{vertex_id:u64BE}`，value 为空。记录所有存活顶点（含无标签 vertex），
是 `MATCH (n)` 全图扫描和 `createAllVertexScanCursor` 的来源；按 vid 天然升序且唯一。

### 边邻接索引（edge_index）

全局一张表。每条逻辑边产生**两条**索引记录（出边 + 入边）：一条 `direction=OUT`，一条 `direction=IN`，指向同一个 edge_id。

```
前缀 {vertex_id}{OUT} → 所有出边
前缀 {vertex_id}{OUT}{label_id} → 某类型出边
前缀 {vertex_id}{OUT}{label_id}{neighbor_id} → 两点间某类型所有边
```

邻接缓存启用时，只保存 OVERFLOW 方向的边；默认 N=0 时保存全量边。

### 边类型索引（etype_{id}）

每个关系类型一张表。仅存出边方向，每条逻辑边一条记录。用于按关系类型扫描所有边。

### 边属性（eprop_{id}）

每个关系类型一张表。prop_id 在 EdgeLabel 内编号；`eprop` 的 key 与 `vertex_data` 的属性 key 无关，保持 `{edge_id}{prop_id}`。

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

### 边索引 Value 编码

边属性索引的 value 存储边的邻接信息，避免二次查询。格式（26 字节）：

```
{src_id:u64BE}{dst_id:u64BE}{seq:u64BE}{label_id:u16BE}
```

编解码由 `ValueCodec::encodeEdgeAdjacency` / `decodeEdgeAdjacency` 实现。

索引扫描时使用 `scanIndex*WithValue` 方法，一次性返回 `(edge_id, adjacency_info)`，用于 `EdgeIndexScanPhysicalOp` 直接产出包含 src/dst/edge 的 Row。

### 编码→查询模式举例

**边邻接索引（edge_index）** 支持以下查询：

```
前缀 {vertex_id}{OUT}                          →  MATCH (a)-[e]->(b) WHERE id(a) = X
前缀 {vertex_id}{OUT}{label_id}                 →  MATCH (a)-[e:KNOWS]->(b) WHERE id(a) = X
前缀 {vertex_id}{OUT}{label_id}{neighbor_id}     →  MATCH (a)-[e:KNOWS]->(b) WHERE id(a) = X AND id(b) = Y
前缀 {vertex_id}                                →  MATCH (a)-[e]-(b) WHERE id(a) = X  （出边+入边）
```

**边类型索引（etype_{id}）** 支持以下查询：

```
无 prefix（全表扫描）                             →  MATCH ()-[e:KNOWS]->()
前缀 {src_id}                                   →  MATCH (a)-[e:KNOWS]->() WHERE id(a) = X
前缀 {src_id}{dst_id}                           →  MATCH (a)-[e:KNOWS]->(b) WHERE id(a) = X AND id(b) = Y
```

**顶点属性索引（vidx_{index_id}）** 支持以下查询：

```
等值扫描 {sortable_value}                        →  MATCH (n:Person) WHERE n.age = 30
范围扫描 {start} < x < {end}                     →  MATCH (n:Person) WHERE n.age > 20 AND n.age < 40
复合等值 {v1}{v2}                                →  MATCH (n:Person) WHERE n.age = 30 AND n.name = 'Alice'
```

**边属性索引（eidx_{edge_label_id}_{prop_id...}）** 支持以下查询：

```
等值扫描 {sortable_value}                        →  MATCH ()-[e:KNOWS]->() WHERE e.weight = 5
范围扫描 {start} < x < {end}                     →  MATCH ()-[e:KNOWS]->() WHERE e.since > 2020
复合等值 {v1}{v2}                                →  MATCH ()-[e:KNOWS]->() WHERE e.weight = 5 AND e.since = 2020
```

边属性索引的 value 包含邻接信息，因此 `EdgeIndexScanPhysicalOp` 可直接产出 src/dst/edge 列，等价于替换了 Expand + Filter 的组合。

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

## 实施记录（2026-10-25/26，sf0.1）

### N=64 与 N=0

- **N=64**：COMPLETE 邻接的 state + key 扫描开销在 c5 的 8,814 个 forum / 89k post
  小批量访问上被放大：c5 329 ms、c14 320 ms，均慢于旧存储；c9 在融合
  `getVertexCoalescePropertiesBatch` 后 244 → 143 ms。
- **N=0**（不写 state/邻接 key，`edge_index` 全量）：c9 131.4 ms（旧 161.7）、
  c5 279.7 ms（旧 264，+6%）、c14 290.7 ms（旧 295）。
- 因此默认关闭邻接缓存（N=0），保留 `vertex_data` 的 labels/properties 合并与融合读取；
  邻接 key/state 代码保留但默认不启用，避免 c5/c14 回退。

### loader

- N=0 时不再写 per-vertex state，**4 RPC 连接可正常导入**：28.3 s，loader 峰值 RSS 222 MB；
- 1 连接：43.2 s，80 MB；
- N>0 的多连接仍会因 state 写冲突需要单连接或后续重试/bulk build。

### 同轮交错 A/B

最终 sf0.1（同轮交错 A/B，5 轮 min）：

| 查询 | eugraph | neo4j | 行一致 |
|---|---:|---:|---|
| complex-5 | 234.0 ms | 83.1 ms | ✅ |
| complex-9 | 187.2 ms | 28.1 ms | ✅ |
| complex-14 | 291.4 ms | 17.7 ms | ✅ |

**c9 更正**：旧的 161.7 ms 来自 filter-boundary 延迟物化缺陷——Sort 所需、Filter 不需要的
`message.id` 被放在 Sort 下方按 LIMIT 后物化，Sort 比较了占位值，Top-20 不是最大日期。
该优化已移除；修复后 c9 与 neo4j 逐行一致。优化前正确基线是 422 ms。

### 共享 cursor 与分页

`AsyncGraphDataStore::scanEdgesBatch` 原先对每个 source 调一次 `createEdgeScanCursor`
（c5 实测 98k 次、累计 184 ms）。现改为：

- 每 4,096 个 source 复用一个 sync cursor，一次 `search_near` 后顺序扫；
- 结果分批返回，Expand 语义不变；
- Expand 单源改为 `scanEdges(..., edge_scratch_)`：调用方拥有 vector，generator 每批最多 4096 条；
  超级节点不会一次性物化全部邻接；
- 热点 cursor 循环使用 `getKeyView`/`getValueView`，不再为每条边拷贝 key/value。

### 微优化对照

把 f1ecea06..eb7ffbee（caller vector、去 key/value 拷贝、分页、Expand/LeftJoin/PE scratch）
与 adafc818 基线在同一数据副本上交错 A/B（两个服务端同时运行、5 轮 min）：

| 查询 | adafc818 基线 | eb7ffbee | 变化 |
|---|---:|---:|---:|
| complex-5 | 236.1 ms | 242.3 ms | +2.6% |
| complex-9 | 194.6 ms | 183.8 ms | -5.5% |
| complex-14 | 285.7 ms | 297.4 ms | +4.1% |

结论：**没有稳定的性能收益**，差异在噪声/轻微回退范围。caller-owned scan buffer 保留的主要价值
是内存上限（4096 条/页），不是提速。更完整的全量 21 条结果见
[LDBC SNB SF0.1 对照](../benchmark/ldbc-snb-sf0.1-comparison.md)。

---

## 关键设计决策

1. **多表隔离替代单 KV 前缀**：每个 Label/EdgeLabel 拥有独立的 WT 表，表名即分区依据，避免单表前缀扫描时的跨 label 干扰，并能独立配置 WT 表选项
2. **prop_id 替代属性名**：KV key 中存 prop_id 而非属性名字符串，字段改名只需修改元数据，无需重写数据
3. **prop_id 永不复用**：删除列后 prop_id 保留空洞，新增列分配新 ID，避免旧数据被误读
4. **边双向索引**：edge_index 表同时维护出边和入边索引，每条逻辑边对应两条记录
5. **边索引邻接冗余**：边属性索引的 value 存储邻接信息（src/dst/seq/label_id），使 `EdgeIndexScanPhysicalOp` 无需二次查询即可产出完整行。代价是每条索引条目多 26 字节
6. **vertex_data 按顶点聚簇**：标签、属性、可选邻接共享同一个 `{vid}` 前缀；删除顶点只需一次前缀扫描，PE 的 labels/props 可以融合读取
7. **邻接缓存默认关闭**：N=0 时 `edge_index` 保存全量边；N>0 的 COMPLETE/OVERFLOW 是可选层，实测 N=64 在 c5/c14 上是负收益
8. **不做历史迁移**：`label_reverse`/`vprop` 的数据不迁移，已有图重新导入
