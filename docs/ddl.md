# DDL（数据定义语言）设计

> 参见 [overview.md](overview.md) 返回文档导航

本文档描述 schema 级别的 DDL 操作设计，包括删除标签类型、删除关系类型、列的增删改等。

---

## 一、删除标签类型（Drop Label）

### 1.1 设计目标

- 删除某个标签类型（Label），速度要快
- 删除后，顶点本身保留（顶点可能还有其他标签）
- 该标签下的属性数据和索引数据被清除

### 1.2 数据分析

删除标签类型涉及三类 KV 数据：

| Key 格式 | 用途 | 能否快速删除 |
|----------|------|-------------|
| `I\|{label_id}\|{vertex_id}` | 标签正向索引（Label → Vertices） | ✅ DeleteRange，key 以 label_id 开头，数据连续 |
| `X\|{label_id}\|{vertex_id}\|{prop_id}` | 属性存储 | ✅ DeleteRange，key 以 label_id 开头，数据连续 |
| `L\|{vertex_id}\|{label_id}` | 标签反向索引（Vertex → Labels） | ❌ label_id 在 key 尾部，分散在不同 vertex_id 下 |

I| 和 X| 占数据量的绝大部分，可用 DeleteRange 瞬间清除。L| 数据量很小（每顶点仅一条记录），但无法范围删除。

### 1.3 方案：Tombstone + 部分快速清理

#### 操作流程

```
1. 在元数据中标记该 label_id 为 "已删除"（写入 tombstone）
2. DeleteRange  I|{label_id}|*   — 清除标签正向索引
3. DeleteRange  X|{label_id}|*   — 清除该标签下所有属性
4. L| 条目保留不动
5. 完成
```

#### 读取时的处理

- **查某标签下的顶点**（I| scan）→ 已被 DeleteRange 清掉，查不到 ✅
- **查某顶点的标签**（L| scan）→ 返回结果中过滤掉 tombstone 的 label_id。每个顶点的标签数通常很少，过滤开销可忽略 ✅
- **查某顶点在某标签下的属性**（X| scan）→ 已被 DeleteRange 清掉，查不到 ✅

#### L| 条目的异步清理（可选）

L| 条目量很小且读取时已被过滤，不影响正确性。可选择：
- 后台任务定期扫描清理
- 或者不做清理（占用空间极小）

### 1.4 元数据变更

```
Label 定义中增加状态字段:

  M|label:{name}
  Value: {label_id}|{status:uint8}|{properties_def}|{primary_key}|{indexes}

  status:
    0x00 = 正常 (ACTIVE)
    0x01 = 已删除 (DELETED)
```

删除标签类型时，将 status 改为 DELETED 而非删除整条记录，保留 tombstone 信息用于 L| 过滤。

### 1.5 测试用例

| # | 测试场景 | 验证点 |
|---|---------|--------|
| 1 | 删除存在的标签类型 | 元数据 status 变为 DELETED，I| 和 X| 数据被清空 |
| 2 | 删除不存在的标签类型 | 返回错误，无副作用 |
| 3 | 删除后查该标签下的顶点 | 返回空（I| 已清） |
| 4 | 删除后查某顶点的标签 | 结果中不含已删除的标签（L| 过滤） |
| 5 | 删除后查顶点属性 | 该标签下属性返回空（X| 已清），其他标签属性不受影响 |
| 6 | 顶点保留 | 删除标签后，顶点仍可通过其他标签访问 |
| 7 | 重复删除同一标签 | 第二次返回错误或幂等成功 |

---

## 二、删除关系类型（Drop EdgeLabel）

### 2.1 设计目标

- 删除某个关系类型（EdgeLabel），速度要快
- 需同时删除该关系类型的所有边（属性和索引）
- 顶点不受影响

### 2.2 数据分析

与删除标签类型（第一节）完全对称：

| Key 格式 | 用途 | 能否快速删除 |
|----------|------|-------------|
| `G\|{edge_label_id}\|{src_id}\|{dst_id}\|{附加id}` | 边类型索引 | ✅ DeleteRange，key 以 edge_label_id 开头 |
| `D\|{edge_label_id}\|{edge_id}\|{prop_id}` | 边属性存储 | ✅ DeleteRange，key 以 edge_label_id 开头 |
| `E\|{vertex_id}\|{direction}\|{edge_label_id}\|{neighbor_id}\|{附加id}` | 邻接索引 | ❌ edge_label_id 在 key 中间，分散在不同 vertex_id 下 |

### 2.3 方案：Tombstone + 部分快速清理

与第一节方案一致：

```
1. 在元数据中标记该 edge_label_id 为 "已删除"（tombstone）
2. DeleteRange  G|{edge_label_id}|*   — 清除类型索引（所有边的扫描入口）
3. DeleteRange  D|{edge_label_id}|*   — 清除所有边的属性
4. E| 条目保留不动
5. 完成
```

#### 读取时的处理

- **按关系类型扫描边**（G| scan）→ 已被 DeleteRange 清掉，查不到 ✅
- **从某顶点遍历边**（E| scan）→ 返回结果中过滤掉 tombstone 的 edge_label_id ✅
- **查某条边的属性**（D| scan）→ 已被 DeleteRange 清掉，查不到 ✅
- **统计度数**（countDegree）→ 扫描 E| 时过滤 tombstone 的 edge_label_id ✅

### 2.4 元数据变更

与 Label 一致，EdgeLabel 定义也增加 status 字段：

```
  M|edge_label:{name}
  Value: {edge_label_id}|{status:uint8}|{properties_def}|{directed}

  status:
    0x00 = 正常 (ACTIVE)
    0x01 = 已删除 (DELETED)
```

### 2.5 测试用例

| # | 测试场景 | 验证点 |
|---|---------|--------|
| 1 | 删除存在的关系类型 | 元数据 status 变为 DELETED，G| 和 D| 数据被清空 |
| 2 | 删除不存在的关系类型 | 返回错误，无副作用 |
| 3 | 删除后按类型扫描边 | 返回空（G| 已清） |
| 4 | 删除后从顶点遍历边 | 结果中不含已删除的关系类型的边（E| 过滤） |
| 5 | 删除后查边属性 | 返回空（D| 已清） |
| 6 | 顶点不受影响 | 删除关系类型后，顶点及其属性仍可正常访问 |
| 7 | 度数统计 | countDegree 过滤掉已删除关系类型的边 |
| 8 | 重复删除 | 第二次返回错误或幂等成功 |

---

## 三、列操作（Column DDL）

> 删除列、重命名列、添加列均基于 prop_id 机制，属于**纯元数据操作**，不涉及 KV 数据的扫描或修改。

### 3.1 设计前提：prop_id 机制

当前 KV 编码中，属性存储使用 prop_id 而非属性名：

```
Vertex 属性:  X|{label_id}|{vertex_id}|{prop_id}
Edge 属性:    D|{edge_label_id}|{edge_id}|{prop_id}
```

prop_id 在每个 Label/EdgeLabel 内单调递增、**永不复用**。属性名与 prop_id 的映射关系保存在元数据中。

这一设计使得列操作无需修改任何 KV 数据，只需修改元数据中的 PropertyDef 即可。

### 3.2 删除列（Drop Column）

#### 操作

从 Label/EdgeLabel 的 PropertyDef 中移除目标 prop_id 的定义。

```
1. 读取 Label/EdgeLabel 定义
2. 找到目标 prop_id，从 properties_def 中移除
3. 写回元数据
4. 完成
```

#### 正确性保证

- 已存储的 `X|{label_id}|{vertex_id}|{prop_id}` 或 `D|{edge_label_id}|{edge_id}|{prop_id}` 保留在 KV 中，成为孤儿数据
- 读取时，只返回 PropertyDef 中定义的 prop_id，已删除的 prop_id 不会出现在结果中
- prop_id 永不复用，即使后续添加新列也不会误读旧数据

#### 孤儿数据后台清理

删除列后产生的孤儿数据占用空间但不可见。参照 TiDB 和 CockroachDB 的方案，采用**标记删除 + 后台 GC** 模式：

```
删除列（即时生效）
  └─ 从 PropertyDef 移除 prop_id → 查询不再返回该列

后台 GC 任务（定期执行）
  └─ 扫描 X|{label_id}|* 或 D|{edge_label_id}|*
  └─ 对比当前 PropertyDef，删除不在定义中的 prop_id 条目
  └─ RocksDB Compaction 自然回收物理空间
```

**GC 任务设计**：

- 触发方式：定时任务（如每小时一次）或手动触发
- 清理范围：仅处理被标记为已删除 prop_id 的数据，不影响正常数据
- 清理顺序：按 label_id 逐个扫描，避免一次性占用过多 IO
- 幂等性：重复执行不影响正确性

**可选：延迟清理窗口**

可配置一个 GC 窗口期（如删除列后 N 小时内不清理），在此期间可通过恢复 PropertyDef 来"撤销删除"。窗口期过后，后台 GC 正式清理数据。

### 3.3 重命名列（Rename Column）

#### 操作

修改 PropertyDef 中目标 prop_id 的名称。

```
1. 读取 Label/EdgeLabel 定义
2. 找到目标 prop_id，修改其属性名
3. 写回元数据
4. 完成
```

**无需修改任何 KV 数据**，因为 KV 中只存 prop_id，不存属性名。这正是当初使用 prop_id 设计的核心收益。

### 3.4 添加列（Add Column）

#### 操作

在 PropertyDef 中新增一个 prop_id 定义。

```
1. 读取 Label/EdgeLabel 定义
2. 分配新的 prop_id（单调递增）
3. 新增 PropertyDef 条目，包含属性名、类型、约束（默认值 或 允许为空）
4. 写回元数据
5. 完成
```

#### 避免回填数据

添加列时必须满足以下条件之一，否则拒绝操作：
- **指定默认值**：读取时，若 KV 中不存在该 prop_id 的数据，返回默认值
- **允许为空**（nullable）：读取时，若 KV 中不存在该 prop_id 的数据，返回 null

```
PropertyDef 结构:
  prop_id     : uint16
  name        : string
  type        : PropertyType
  default_value : optional<PropertyValue>    // 默认值
  nullable    : bool                          // 是否允许为空

约束: default_value.has_value() || nullable == true
```

这样添加列时**无需回填已有数据**，新列对已有记录自动生效（返回默认值或 null）。

### 3.5 测试用例

#### 删除列

| # | 测试场景 | 验证点 |
|---|---------|--------|
| 1 | 删除标签下的列 | PropertyDef 中该 prop_id 消失 |
| 2 | 删除后读取顶点属性 | 结果中不含已删除的列 |
| 3 | 删除后其他列不受影响 | 同一标签下的其他属性正常返回 |
| 4 | 删除关系类型下的列 | EdgeLabel 的 PropertyDef 中该 prop_id 消失，读取边属性不含该列 |
| 5 | 删除不存在的列 | 返回错误 |
| 6 | prop_id 不复用 | 删除 prop_id=2 后添加新列，新列的 prop_id=3 而非 2 |

#### 重命名列

| # | 测试场景 | 验证点 |
|---|---------|--------|
| 7 | 重命名标签下的列 | PropertyDef 中 prop_id 不变，名称改变 |
| 8 | 重命名后读取 | 通过新名称能获取到旧数据（KV 数据不变） |
| 9 | 重命名关系类型下的列 | 同上 |
| 10 | 重命名为已存在的名称 | 返回错误 |

#### 添加列

| # | 测试场景 | 验证点 |
|---|---------|--------|
| 11 | 添加带默认值的列 | 新列对所有已有顶点返回默认值，KV 中无实际数据 |
| 12 | 添加允许为空的列 | 新列对所有已有顶点返回 null |
| 13 | 添加既无默认值又不允许为空的列 | 返回错误，拒绝操作 |
| 14 | 添加后新写入的数据 | 该列正常存储到 KV |
| 15 | 添加列 prop_id 递增 | 新列的 prop_id 大于已有的最大 prop_id |
| 16 | 添加到关系类型 | 与标签下添加列行为一致 |
