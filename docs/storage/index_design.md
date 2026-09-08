# 二级索引设计

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

源码：`src/storage/kv/index_key_codec.hpp`、`src/storage/meta/async_graph_meta_store.cpp`、
`src/query/physical_plan/operator/index_scan_physical_op.*`、
`src/query/physical_plan/operator/vertex_index_maintenance.hpp`、
`src/query/physical_plan/operator/edge_index_maintenance.hpp`。

---

## 1. 目标

- 顶点索引支持 **弱模式**（默认）与 **强模式** 两种属性访问方式。
- 边索引保持单类型语义：边只有一个类型，属性必须属于该边类型。
- 索引物理表名与索引定义列解耦：顶点索引使用 `vidx_{index_id}`。
- 索引写入维护覆盖 CREATE / SET / REMOVE / DELETE，以及标签增删。
- 唯一索引在写入路径冲突时回滚数据变更（顶点路径）。

## 2. 索引状态模型

```cpp
enum class IndexState : uint8_t {
    WRITE_ONLY  = 0,  // 创建中：写入路径维护索引，查询不可用
    PUBLIC      = 1,  // 已就绪：完全可用
    DELETE_ONLY = 2,  // 删除中：写入路径不维护，仅清理残留条目
    ERROR       = 3,  // 异常：唯一索引回填时发现重复值或 weak accessor 冲突
};
```

转换：

```text
CREATE INDEX → WRITE_ONLY → (回填成功) → PUBLIC
                          → (冲突/重复) → ERROR
DROP INDEX   → 删除元数据 + 删除物理表
```

事务行为：

| 索引状态 | 读事务 | 写事务 |
|----------|--------|--------|
| WRITE_ONLY | 不使用索引 | 维护索引条目 |
| PUBLIC | 可使用 IndexScan | 维护索引条目 |
| DELETE_ONLY | 不使用索引 | 仅删除条目 |
| ERROR | 不使用索引 | 不维护条目 |

## 3. 索引键编码

`IndexKeyCodec`（`src/storage/kv/index_key_codec.hpp`）实现可排序编码：

```text
单属性：{sortable_value}{entity_id:uint64 BE}
复合：  {sortable_value1}{sortable_value2}...{entity_id:uint64 BE}
```

| 属性类型 | 编码 |
|----------|------|
| null | `0xFF` |
| bool | `0x00` / `0x01` |
| int64 | `0x00` + 8 字节 BE，符号翻转 |
| double | `0x01` + 8 字节 IEEE 754，符号翻转 |
| string | `0x02` + 原始字节 |

等值查询使用 `encodeEqualityPrefix` 前缀扫描；范围查询使用 `search_near` + 边界检查。

边索引 value 存储邻接信息（`src_id`, `dst_id`, `seq`, `label_id`），`EdgeIndexScanPhysicalOp`
一次扫描即可产出 src/dst/edge，无需回表。

## 4. DDL 语法

```cypher
-- 弱模式：Label 是过滤标签；属性按名从顶点所有标签中查找
CREATE INDEX idx_name FOR (n:Label) ON (n.prop)
CREATE INDEX idx_name FOR (n:Label) ON (n.prop1, n.prop2)

-- 强模式：LabelA 是过滤标签；属性必须来自 LabelB 的属性表
CREATE INDEX idx_name FOR (n:LabelA) ON (n::LabelB.prop)
CREATE INDEX idx_name FOR (n:LabelA) ON (n::LabelB.p1, n::LabelC.p2)

-- 唯一索引同理
CREATE UNIQUE INDEX idx_name FOR (n:Label) ON (n.prop)
CREATE UNIQUE INDEX idx_name FOR (n:LabelA) ON (n::LabelB.prop)

-- 边索引（单类型，无强弱模式）
CREATE INDEX idx_name FOR ()-[r:TYPE]-() ON (r.prop)

DROP INDEX idx_name
SHOW INDEXES
```

解析由 `IndexDdlParser` 完成。`IndexPropertyAccessor` 承载每个列的弱/强信息：

```cpp
struct IndexPropertyAccessor {
    bool is_strong = false;
    std::string source_label;   // 强模式来源标签名
    std::string property_name;
};
```

## 5. 核心模型

| 概念 | 说明 |
|------|------|
| index_id | 索引唯一整数 ID，持久化于 `M|next_ids` |
| filter_label | `FOR` 子句标签；实体必须具备该标签才会进入索引 |
| accessor | 每个索引列的取值规则 |
| 弱 accessor | `{property_name}`：从实体所有标签中按名解析 |
| 强 accessor | `{source_label_id, property_name}`：从指定标签属性表读取 |
| accessors 顺序 | 与 `ON (...)` 顺序一致；索引 key、回填、维护、IndexScan 都按该顺序 |

## 6. 属性 ID

- 存储层 `prop_id` 仍在 Label / EdgeLabel 内独立编号；`vprop_{id}` / `eprop_{id}` 编码不变。
- 顶点索引元数据不存 `prop_id`，只存属性名；强模式额外存 `source_label_id`。
- 弱 accessor 在 DDL / 计划构建 / 维护时按当前 schema 解析为 `(label_id, prop_id)` 候选集合。
- 属性 ID 不要求图内唯一。

## 7. 元数据与表名

索引定义持久化在所属 LabelDef / EdgeLabelDef 的 `indexes` 中：

```cpp
struct IndexDef {
    uint32_t index_id;
    std::string name;
    std::vector<IndexAccessorDef> accessors; // 顶点索引使用
    std::vector<uint16_t> prop_ids;          // 仅边索引使用
    bool unique;
    IndexState state;
};
```

`M|next_ids` 包含 `next_index_id`；`M|index:{name}` 保留名称索引。

物理表名：

```text
顶点索引：table:vidx_{index_id}
边索引：  table:eidx_{edge_label_id}_{prop_id...}  // TODO: 迁移到 eidx_{index_id}
```

内存中维护快速关联：

```cpp
std::unordered_map<LabelId, std::vector<IndexId>> label_to_indexes;
std::unordered_map<PropKey, std::vector<IndexId>, PropKeyHash> prop_pair_to_indexes;
```

其中 `PropKey = {LabelId label_id; uint16_t prop_id;}`，用于属性写入时快速找到受影响索引。

## 8. 建索引与回填

1. 解析 DDL，得到 filter label 与 accessors。
2. 强 accessor 校验：source label 存在且包含该属性，否则报错。
3. 弱 accessor 不要求任何标签已定义该属性；解析为空则索引为空。
4. 分配 `index_id`，持久化元数据，state = WRITE_ONLY。
5. 创建物理表 `vidx_{index_id}`（顶点）或现有边索引表。
6. 回填：扫描 filter label 下所有实体，按 accessors 顺序取值：
   - 强：读 `vprop_{source_label_id}` 的 `(entity_id, prop_id)`；
   - 弱：遍历实体所有标签，按属性名找 `prop_id`：
     - 单标签命中：取该值；
     - 多标签命中且值相同：取该值；
     - 多标签命中且值冲突：置 ERROR，回填失败；
   - 任一 accessor 缺值：跳过该实体；
   - 全部有值：插入索引条目。
7. 唯一索引回填检查重复，冲突置 ERROR。
8. state = PUBLIC。

## 9. 写入路径索引维护

统一原则：**先取旧值/旧条目，再写数据，最后删除旧条目并插入新条目**。

| 变更 | 维护动作 |
|------|---------|
| CREATE 顶点/边 | 计算条目；唯一索引先检查约束；插入数据后插入条目 |
| DELETE 顶点/边 | 删除前收集并删除相关索引条目 |
| SET/REMOVE 属性 | 收集旧条目 → 写属性 → 收集新条目 → 删旧插新 |
| SET/REMOVE 标签 | 同上；新增标签插入新条目，移除标签删除旧条目 |
| 唯一冲突 | 顶点路径回滚数据变更后抛 `ConstraintVerificationFailed` |

顶点维护 helper：`vertex_index_maintenance.hpp`
（`collectVertexIndexEntries` / `collectVertexIndexEntriesFromLabelProps` /
`insertVertexIndexEntriesChecked` / `deleteVertexIndexEntries`）。

边维护 helper：`edge_index_maintenance.hpp`（`collectEdgeIndexEntries` 等）。

## 10. 查询优化

`PhysicalPlanner` 的 `tryBoundIndexScan`：

- 从 Filter 谓词提取 `property op literal`；
- 支持未降级的 `BoundPropertyRef`，也支持 column-rewrite 后的 `BoundColumnRef`
  （通过 PEPlan 反解出属性名 / `(label_id, prop_id)`）；
- 按 accessors 顺序匹配：
  - 弱 accessor 按属性名匹配；
  - 强 accessor 要求 `cond.label_id == source_label_id`；
- 命中 PUBLIC 索引则替换为 `IndexScanPhysicalOp`（使用 `index_id` 扫描 `vidx_{index_id}`）。

`tryBoundEdgeIndexScan` 处理 `Filter(Expand)` 的边属性索引。

## 11. 实现状态与待办

### 已完成

- 顶点弱/强模式索引、`index_id`、`vidx_{index_id}`
- Column-rewrite 谓词反解，点查触发 IndexScan
- 顶点写入路径维护（属性 / 标签 / DELETE）
- 边写入路径维护（边属性 SET/REMOVE / DELETE）
- 顶点唯一冲突回滚（SET prop / SET map / SET label）
- 边唯一冲突：检查 + 跳过插入（未回滚）
- 强模式查询 `n::Label.prop`
- 强弱混合复合索引

### 待办

1. **边索引 `index_id` 化**：迁移 `eidx_{edge_label}_{prop_id}` → `eidx_{index_id}`，
   新增 `scanEdgesByIndexId*`，更新 `EdgeIndexScanPhysicalOp` / planner；完成后删除 `IndexDef::prop_ids`。
2. **边唯一冲突回滚**：回滚已写入的边属性。
3. **运行时索引维护去名称化**：当前 SET/REMOVE/DELETE 维护与回填仍按属性名解析弱 accessor；
   应在内存 `IndexDefRuntime` 中把强 accessor 预解析为 `(source_label_id, source_prop_id)`、
   弱 accessor 预解析为 `[(label_id, prop_id), ...]`，并实现 `label_to_indexes` / `prop_pair_to_indexes`
   整数反向 map，使写入维护和回填不再做属性名字符串比较。
4. **DdlWorker 后台异步回填**。
5. **崩溃恢复**：重启后恢复未完成 DDL 状态。
6. **Thrift RPC 索引接口**。
7. **延迟物理删表**：DROP INDEX 后延迟清理 WT 表（含边表与旧表）。
8. **写路径弱 accessor 冲突显式报错**：当前 SET/REMOVE 维护遇到多标签同名不同值时静默跳过条目；
   设计目标是像回填一样报错（唯一索引置 ERROR，非唯一索引也应给出明确错误）。
9. **属性 rename 与索引联动**：未来支持属性改名时，强索引应更新显示名并保持
   `(source_label_id, prop_id)` 不变；弱索引继续引用旧属性名；属性名注册表/元数据不回收旧 ID。
10. **SHOW INDEXES / db.indexes() 展示 accessor 详情**：当前只输出属性名，强模式来源标签（如 `n::Post.creationDate`）
    无法从输出中区分。
11. **测试补强**：弱模式 UNIQUE 索引的多标签冲突回填、强/混合复合 UNIQUE、边唯一冲突回滚。
12. **显式主键语义（可选）**：当前 loader 用 `UNIQUE INDEX` 表达主键；如需与普通唯一索引区分，需要新增
    PRIMARY KEY DDL 和元数据。
