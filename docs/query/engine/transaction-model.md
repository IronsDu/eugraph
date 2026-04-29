# 事务模型

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

---

## 一、事务基础

### 接口层

- **Sync 层** (`ISyncGraphDataStore`)：`beginTransaction()` → `GraphTxnHandle` → `commitTransaction(txn)` / `rollbackTransaction(txn)`
- **Async 层** (`IAsyncGraphDataStore`)：`beginTran()` → `setTransaction(txn)` → 所有存储操作自动使用该 txn → `commitTran(txn)` / `rollbackTran(txn)`

### 实现

- 每个事务创建独立的 **WT_SESSION**
- 隔离级别：`isolation=snapshot`（WiredTiger snapshot isolation）
- `GraphTxnHandle` = `void*`，实际指向堆上 `TxnState`（包含 WT_SESSION + cursor 缓存）
- commit/rollback 后 `TxnState` 被释放，handle 失效

---

## 二、嵌入式模式事务

`QueryExecutor::executeAsync()` 流程：

```
beginTran → setTransaction → 物理计划 → execute (全量收集) → commitTran
```

- 事务 cover 整个查询执行期间
- 全量结果物化到内存后才提交
- 计划阶段出错 → rollback

### 事务下推到物理算子

setTransaction 将 handle 存入 `AsyncGraphDataStore::txn_`，后续所有算子内的存储操作（scan、getProperties、insert）都使用该事务。`AsyncGraphDataStore` 的方法内部从 `txn_` 查找 `WT_SESSION*`。

---

## 三、流式模式事务

`prepareStream()` 流程：

```
beginTran → setTransaction → 物理计划 → 创建 StreamContext (持有 txn + gen) → 返回
```

事务提交在 handler 的 `makeStreamGenerator` 中：
- generator 正常耗尽 → `commitTran`
- 客户端断连 / generator 提前销毁 → 隐式 rollback（WT session 随 coroutine frame 析构关闭）

---

## 四、批量操作事务

`batchInsertVertices` / `batchInsertEdges` **忽略外部 setTransaction 的事务**，内部自行 begin+commit：

```
batchInsertVertices:
  beginTransaction → 批量 insert → commitTransaction（独立原子提交）
```

这意味着批量操作总是独立事务，不与 Cypher 查询共享事务。

---

## 五、DDL 事务特性

DDL 操作（createLabel、createIndex 等）**非事务性**：
- label/edge label 创建直接操作 meta store 和 data store，不经过 beginTran/commitTran
- CREATE INDEX 同步回填过程中使用独立事务
- DROP INDEX 不实际删除 WT 索引表（仅删除元数据）

---

## 六、Schema 与事务一致性

每个事务开始时加载 schema 快照（label_defs、edge_label_defs），事务全程使用同一份快照：

- **只读事务**：物理计划在事务开始时一次性构建，WT snapshot isolation 保证数据一致性
- **读写事务**：读路径（物理计划）和写路径（索引维护）使用同一份 schema

DDL 状态变更（如索引状态从 WRITE_ONLY → PUBLIC）不影响已在运行的事务——它们持有旧 schema 的副本。

---

## 七、易错点

1. **GraphTxnHandle 是原始指针**：commit/rollback 后立即释放，任何后续使用是 use-after-free。代码中 commit 总是放到最后一步，但缺乏编译期保护。

2. **流式模式客户端断连 = 隐式 rollback**：`commitTran` 不会执行，依赖 WT session 析构回滚。正常但无显式保护。

3. **批量操作独立事务**：与 Cypher 查询的事务隔离。如果查询中调用了 batch 方法，它们各自提交，不参与外层事务。
