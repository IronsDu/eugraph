# vertex_data 存储设计（标签 / 属性 / 邻接）

> 状态：labels/properties + 邻接 key-per-edge/state 已实现；loader 后置批量构建待补。
> 替换 `label_reverse` + `vprop_{id}`，邻接采用 `vertex_data` 内 key-per-edge +
> `AdjacencyCacheState`，溢出回退 `edge_index`。不做历史数据迁移，已有图需重新导入。

## 1. 目标与表

单张 `table:vertex_data`，按顶点前缀聚簇：

```text
{v:u64BE}{kind:u8}{...}

0x00 LABEL      {v}{0x00}{label:u16BE}
0x01 PROPERTY   {v}{0x01}{label:u16BE}{prop:u16BE}

0x02 OUT        {v}{0x02}                              -> state + count
                {v}{0x02}{label:u16BE}{neighbor:u64BE}{seq:u64BE} -> edge_id

0x03 IN         {v}{0x03}                              -> state + count
                {v}{0x03}{label:u16BE}{neighbor:u64BE}{seq:u64BE} -> edge_id
```

- `label_fwd_{id}`、`vertex_existence`、`etype_{id}`、`eprop_{id}`、二级索引保留；
- `label_reverse`、`vprop_{id}` 删除；
- `edge_index` 改为 **overflow-only**：只保存 COMPLETE 缓存不足以容纳的顶点的邻接；
- 一条边的两个方向（OUT/IN）分别独立判断 COMPLETE/OVERFLOW。

## 2. 邻接状态与分层

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

N 第一阶段取全局常量（后续可按 label/方向配置）。

## 3. 读写路径

- `scanEdges` / `createEdgeScanCursor`：
  - 读 state；
  - COMPLETE：cursor 从 `{v}{0x02|0x03}` 开始，跳过 state key，顺序扫边 key；
  - OVERFLOW：扫 `edge_index`；
  - `BOTH`：OUT/IN 分别判断后合并。
- `countDegree`：COMPLETE 直接用 state.count；OVERFLOW 扫 edge_index；
- `deleteVertex`：`{v}` 前缀删除 label/prop/state/edge key；边由 DETACH DELETE 先删；
- `DROP LABEL`：`label_fwd` 枚举顶点，删 label/属性前缀；
- `DROP EDGE LABEL`：`etype` 枚举该 label 的边，按端点 state 删除缓存 key 或 edge_index 条目，再 drop etype/eprop；
- loader：导入时只写 `etype/eprop`，导入后按 degree 一次性构建 `vertex_data` 邻接 + `edge_index` + state，避免逐边溢出迁移。

## 4. 查询/算子/优化器利用

1. **PE 的 labels + props**：一次 `{v}{0x00}` / `{v}{0x01}` 前缀扫描，替代
   `label_reverse` + `vprop` 两次跨表点查；后续增加 `loadVertexData(v, needs_mask)`，
   只填充请求的 labels/props/out/in。
2. **Expand**：COMPLETE 时直接读 `vertex_data` 邻接 key；低 degree 不再有
   `edge_index` 条目，且和顶点属性同表；OVERFLOW 保持现有 `edge_index` 路径。
3. **countDegree / 基数估计**：COMPLETE 用 state.count（O(1)），CBO 可在不扫描的情况下
   获得准确 out/in degree，用于 join order、Expand 方向/驱动侧选择。
4. **目标属性下推**：`Expand` 后紧接 `friend IN friends` / `friend.x = ...` 时，
   可在同一 `vertex_data` 前缀内读取目标属性，减少一次跨表访问。
5. **PC / 半连接**：`CorrelatedVertexProbe` 可按端点直接读 `vertex_data`，
   缓存 Person 锚点；配合 state 判断是否需要 edge_index。
6. **覆盖读**：`{v}{0x01}{label}` 一次读某标签全部属性，适合 `RETURN n` /
   ConstructVertex；`{v}{0x00}` 读标签集合。
7. **优化器**：把 state/count 作为统计信息（free degree），用于
   - 选择 Expand 的驱动侧（高 degree 侧后展开）；
   - 估算 `Expand` 输出行数；
   - 判断是否需要 `BatchExpand` / 邻接扫描；
   - PC anchor 选择（已有端点时优先 Probe）。

## 5. 实施阶段

1. `vertex_data` labels/properties + 删除旧表（已完成）；
2. key codec：OUT/IN state + edge key；
3. `insertEdge/deleteEdge` 分层与溢出迁移；
4. `scanEdges` / `createEdgeScanCursor` / `countDegree` state-aware；
5. loader 后置构建 + `DELETE/DROP EDGE LABEL` 清理；
6. 重新导入 LDBC，基准 + TCK；
7. 查询/优化器接入（PE mask、degree 估计、目标属性下推）。

## 6. 风险

- 首次溢出的原子迁移与并发：同顶点并发插入需串行化/重试；
- OVERFLOW 后 edge_index 与 vertex_data 的边界必须严格：读路径不能混读；
- `DROP EDGE LABEL`、`deleteVertex`、loader 重建；
- COMPLETE 路径与 edge_index 读法同构，**不会更快**，收益是统一存储、低 degree 无
  edge_index 条目和写放大；高 degree 仍走 edge_index。

## 7. 实施记录（2026-10-25，sf0.1）

### 已完成

- labels/properties 合并到 `vertex_data`，删除 `label_reverse`/`vprop`；
- OUT/IN 邻接 key + `AdjacencyCacheState`，`edge_index` 为 overflow-only；
- `scanEdges`/`createEdgeScanCursor`/`countDegree` 按 state 分派；
- 缓存 cursor 改为流式（不再把单个顶点的边 materialize 成 vector），
  c5 从 443 ms 降到 327 ms（仍高于旧 edge_index 的 264 ms）。

### LDBC 重新导入

- 4 RPC 连接：在 `vertex_data` 的 per-vertex state 上产生 WT 写冲突
  （`WT_ROLLBACK -31800`），loader 失败；
- 1 RPC 连接：成功，**43.2 s**，loader 峰值 RSS 80 MB，server 峰值 RSS ~530 MB；
- 超级节点没有额外内存：COMPLETE 最多 N 条，超过即 OVERFLOW。

### 当前基准（新存储 vs 旧存储）

| 查询 | 新存储 | 旧存储 |
|---|---:|---:|
| complex-9 | 244 ms | 161.7 ms |
| complex-5 | 327 ms | 264 ms |
| complex-14 | 307 ms | 295 ms |

结论：功能正确，但**当前读路径整体回退**。主要嫌疑：

1. `vertex_data` 单表同时含 label/property/邻接，B-tree 变大，点查 page 局部性下降；
2. label + property 仍是两次独立前缀扫描，没有融合；
3. COMPLETE 邻接读比纯 `edge_index` 多一次 state 读/解码。

下一步必须先做读路径优化（融合 labels+props 的 `loadVertexData`、邻接 state/边同 cursor、
N 调优），在回退消除前不应合并。

### 读路径融合（2026-10-25）

- 新增 `getVertexCoalescePropertiesBatch(vids, candidates)`：一次 `{v}{0x01}` 前缀扫描
  同时完成 labels 判定 + 候选属性取值，PE 的 coalesce 不再先 `getVertexLabelsBatch`
  再 `batchGetVertexProperties`。
- c9：244 ms → **143 ms**（旧存储 161.7 ms），已恢复并略优于旧路径。
- c14：320 ms（N=64）/ 305 ms（N=0），旧存储 295 ms，仍略慢。
- c5：329 ms（N=64）/ 346 ms（N=0），旧存储 264 ms，仍明显慢；
  主要剩余嫌疑是大量 per-source label 过滤/小批量扫描在更大的 `vertex_data` 表上变慢。
