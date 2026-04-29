# 辅助索引设计

> [当前实现（部分）] 参见 [README.md](../README.md) 返回文档导航

---

## 一、索引状态模型

参考 F1 论文（*Online, Asynchronous Schema Change in F1*）的中间状态机制，做单机场景简化：

### IndexState

```cpp
enum class IndexState : uint8_t {
    WRITE_ONLY  = 0,  // 创建中：写入路径维护索引，查询不可用
    PUBLIC      = 1,  // 已就绪：完全可用
    DELETE_ONLY = 2,  // 删除中：写入路径不维护，仅清理残留条目
    ERROR       = 3,  // 异常：唯一索引回填时发现重复值
};
```

### 状态转换

```
CREATE INDEX → WRITE_ONLY → (回填完成) → PUBLIC
                                      → (发现重复) → ERROR
DROP INDEX → DELETE_ONLY → (清理完成) → 删除元数据
```

### 事务行为

| 索引状态 | 读事务 | 写事务 |
|----------|--------|--------|
| WRITE_ONLY | 不使用索引（全表扫描） | 写入+删除均维护索引条目 |
| PUBLIC | 可用索引（IndexScan） | 写入+删除均维护索引条目 |
| DELETE_ONLY | 不使用索引 | 仅删除索引条目，不写入新条目 |
| ERROR | 不使用索引 | 不维护索引条目 |

---

## 二、事务级 Schema 绑定

单进程场景下的核心设计：**每个事务开始时加载 schema 快照，全程使用同一份**。

- DDL Worker 修改元数据后原子替换全局 `shared_ptr<GraphSchema>`
- 已在运行的事务持有旧 schema 的 `shared_ptr`，不受影响
- 新事务开始时自然加载最新 schema

安全性：
- 事务持有旧 schema、DDL 升级索引到 PUBLIC：安全——旧 schema 中索引是 WRITE_ONLY，写入路径仍维护索引
- 事务持有旧 schema、DDL 降级索引到 DELETE_ONLY：安全——旧 schema 中索引是 PUBLIC，写入路径仍维护索引
- 只读事务、索引状态变化：安全——WT snapshot isolation 保证数据一致性

---

## 三、索引键编码

`IndexKeyCodec`（`src/storage/kv/index_key_codec.hpp`）：

```
索引键格式：{sortable_property_value}{entity_id:uint64 BE}
```

### 可排序属性值编码

| 类型 | 编码 |
|------|------|
| null | `0xFF`（排最前） |
| bool | `0x00` / `0x01` |
| int64 | `0x00` + 8 字节 BE，符号翻转 |
| double | `0x01` + 8 字节 IEEE 754，符号翻转 |
| string | `0x02` + 原始字节（v1 仅 ASCII） |

等值查询：`encodeEqualityPrefix` → 前缀扫描。范围查询：`search_near` + 边界检查。

---

## 四、索引 DDL

### 语法

```cypher
CREATE INDEX idx_name FOR (n:Label) ON (n.prop)
CREATE UNIQUE INDEX idx_name FOR (n:Label) ON (n.prop)
CREATE INDEX idx_name FOR ()-[r:TYPE]-() ON (r.prop)
DROP INDEX idx_name
SHOW INDEXES
```

`IndexDdlParser`（手写字符串解析器）在 ANTLR 解析前拦截。

### 当前实现：同步回填

`QueryExecutor::handleIndexDdl()` 在当前请求内同步执行所有步骤：

```
CREATE INDEX:
  1. 参数校验（label/property 存在性）
  2. 创建索引元数据（state=WRITE_ONLY）
  3. 创建 WT 索引表
  4. 同步回填：扫描该 label 下所有顶点 → 对每个顶点插入索引条目
  5. 更新 state=PUBLIC

DROP INDEX:
  1. 删除索引元数据（不物理删 WT 表，不清理已有条目）
```

### 写入路径索引维护

当前仅在 `CreateNodePhysicalOp::execute()` 中维护索引：插入顶点后遍历 `label_defs` 中 WRITE_ONLY/PUBLIC 状态的索引，写入索引条目。

**未维护的场景**：deleteVertex、putVertexProperty、deleteVertexProperty、Edge 写入路径。

---

## 五、存储接口

`ISyncGraphDataStore` 索引方法：
- `createIndex(table_name)` / `dropIndex(table_name)` — DDL 级表操作
- `insertIndexEntry(table, value, entity_id)` / `deleteIndexEntry(table, value, entity_id)` — 条目 CRUD
- `checkUniqueConstraint(table, value)` — 唯一性校验
- `scanIndexEquality(txn, table, value, callback)` / `scanIndexRange(...)` — 索引扫描
- `dropAllIndexEntries(table)` — 清理所有条目

WT 表格式：`key_format=u,value_format=u`（raw bytes）。

---

## 六、查询优化（IndexScanPhysicalOp）

`PhysicalPlanner` 在遇到 `Filter(LabelScan)` 模式时，分析谓词表达式：

- 提取 `PropertyAccess(variable, prop) op Literal` 模式
- 检查该属性是否存在 **state==PUBLIC** 的索引
- 匹配则用 `IndexScanPhysicalOp` 替换 `Filter+LabelScan`

支持 `EQ`（等值扫描）、`GT/GTE/LT/LTE`（范围扫描）和 `GT/GTE AND LT/LTE` 组合。

---

## 七、实现状态

### 已完成
- IndexState 枚举、IndexDef.state、EdgeLabelDef.indexes
- IndexKeyCodec（可排序编码 + 等值/范围扫描）
- ISyncGraphDataStore 索引方法 + SyncGraphDataStore 实现
- IAsyncGraphDataStore async 包装
- IAsyncGraphMetaStore 索引元数据 CRUD（createVertexIndex/createEdgeIndex/dropIndex/listIndexes）
- MetadataCodec 索引序列化/反序列化
- IndexDdlParser（CREATE/DROP/SHOW INDEX）
- QueryExecutor DDL 路由 + 同步回填
- IndexScanPhysicalOp + PhysicalPlanner 索引优化
- CreateNodePhysicalOp 写入路径索引维护

### 待完成
- 写入路径完整索引维护（deleteVertex、putVertexProperty 等）
- DdlWorker 后台异步执行
- 崩溃恢复（重启后恢复未完成 DDL 状态）
- Thrift RPC 索引接口
- 延迟物理删表（DROP INDEX 后不立即删 WT 表，重启时清理）
