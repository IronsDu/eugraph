# Dynamic IndexJoin：单次 Source 消费

> 状态：2026-11-25。动态 key 的 IndexJoin 已从
> `ReexecuteSource + Apply + HashJoin` 改成单个 `DynamicIndexJoinPhysicalOp`。
> 磁盘 spill 是后续阶段，接口按可替换 row store 设计。

## 1. 问题

动态 key 路径需要同一份 source 数据用于两件事：

```text
source rows
  ├─ 构建 join 的 source 侧
  └─ 提取 distinct dynamic index keys
```

旧实现用 `ReexecutePhysicalOp` 再次调用 source：

```text
HashJoin(build=source)
  probe=Apply(
    keyStream = Distinct(Project(Reexecute(source)))
    ...
  )
```

问题：

- `Reexecute` 非 owning，`children()` 为空；
- source 被调用两次；
- 隐式假设 source 可重入/确定；
- 第二次执行没有进入 CBO 成本；
- EXPLAIN 才在后续 PR 中显式暴露这个真实路径。

## 2. 当前实现：DynamicIndexJoinPhysicalOp

```text
DynamicIndexJoin(
  source,                 // owning child
  index_chain,            // IndexScanValues + reverse Expand
  source_key_col,
  dynamic_key_col,
  match_key_col,
  max_build_rows
)
```

执行：

```text
for chunk in source.executeChunk():       // 只执行一次
    for row in chunk:
        hash_table[source_key(row)] = source_payload(row)
        distinct_keys.insert(dynamic_key(row))

index_scan.setValues(distinct_keys)

for chunk in index_chain.executeChunk():  // index probe + reverse expand
    for row in chunk:
        for source_row in hash_table[match_key(row)]:
            emit(source_row, row)
```

不变量：

- `source` 是普通 owned child；
- source 只执行一次；
- 无 `ReexecuteSource`；
- `children()` 同时暴露 source 和 index chain；
- source 行在 `max_build_rows` 内；超限明确报错，不静默重执行。

## 3. 内存与后续 spill 路线

当前 `max_build_rows` 是按行数的硬预算，超限抛错。下一阶段按以下路线替换为可 spill
存储，不改变算子外部语义。

### Phase 2：SpillableRowStore 抽象

```cpp
class SpillableRowStore {
public:
    void append(size_t partition, DataChunk chunk);
    void finalize();
    RowIterator scan(size_t partition);
    void spill(size_t partition);
    bool spilled(size_t partition) const;
};
```

先提供纯内存实现，替换当前 `unordered_map<RowDigest, vector<vector<Value>>>`。

### Phase 3：按 join key 分区

```text
source row → hash(join_key) % N
match row  → hash(join_key) % N
```

逐 partition：

```text
load source partition
probe match partition
```

上限从“整个 source”降为“最大单 partition”。

### Phase 4：磁盘 spill

- partition 超过内存预算时写临时文件；
- 格式：schema + 行数 + column chunks + terminator；
- 临时目录/配额/清理由 store 管理；
- spill 失败回退为明确错误，不重执行 source；
- CBO 成本加入 spill 成本。

### 验收

- 小预算强制 spill 后结果与内存版逐行一致；
- source 执行计数器 == 1；
- EXPLAIN 显示 `mode=dynamic-index-key`；
- 不出现 `ReexecuteSource`。

## 4. 当前限制

- source payload 全部在内存；
- dynamic key 去重集合在内存；
- `max_build_rows` 只按行数，不按字节；
- 还没有分区和临时文件；
- CBO 仍使用过渡常数，未计 spill。

这些限制由后续 P3 统计/成本和 spill 阶段处理，不在当前正确性修复中混入。

## 5. 验证

```text
query_executor_tests  687/687 passed
c2/c6/c9 行数      20/10/20
c6 warm            ~13.7 ms
c6 EXPLAIN         DynamicIndexJoin(...), 无 ReexecuteSource
```
