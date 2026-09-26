# 已知缺陷待办

> 排查 complex-10 正确性缺陷过程中**顺带确认**、但与 complex-10 **无关**的既有缺陷。
> 每条都附最小复现或代码位置，可直接开工。
>
> 来源复盘见 [comprehension-defect-debugging-notes.md](comprehension-defect-debugging-notes.md)。

## 1. 反向关系模式不匹配

**现象**：同一份数据上，正向有结果、反向返回空。

```cypher
MATCH (post:Post)-[:CREATED]->(p:P)   // 有结果
MATCH (p:P)<-[:CREATED]-(post:Post)   // 返回空   ← 缺陷
```

**复现**（单元测试 fixture 规模即可，见 `tests/test_query_executor.cpp` 中
`ListComprehensionPatternPredicate*` 的建图方式）：建 `post -[:CREATED]-> person` 一条边，
两种写法查询同一对节点。

**影响**：所有以 `<-[:R]-` 书写的查询都可能漏结果，属**正确性**问题，优先级高。
本次排查的 fixture 全部改写成正向以绕开它。

**定位起点**：`ExpandPhysicalOp` 的 `direction` 处理与 `Direction::IN` 的扫描方向；
`bindExpand` 中 `LEFT_TO_RIGHT` / `RIGHT_TO_LEFT` 的语义与 `physical_planner` 的 `scan_dir` 换算。

## 2. 关联变量注册类型硬编码 `Vertex()`

**位置**：`src/query/planner/binder/bind_match.cpp`，`bindExistsSubPlan` 注册
`extra_corr_vars` 处（约 1314 行）。

```cpp
ci.name = var_name;
ci.type = BoundType::Vertex();     // 硬编码：对 LIST / EDGE 等类型不成立
```

**影响**：当被关联的变量不是顶点（例如外层列表推导的列表、边）时类型错误，
下游 `Unwind` 等算子按错误类型读取。实测：`posts`（LIST）被注册成 VERTEX。

**修法**：类型取自 `saved_ctx.symbols` 中该变量的真实类型，回退到 `Vertex()`。

## 3. 按 slot 反查变量名存在歧义

**位置**：`bind_match.cpp` 构建 `BoundCorrelatedSourceOp` 的循环（约 1309 行）。

```cpp
for (const auto& [outer_slot, sub_slot] : correlation) {
    for (const auto& [name, info] : ctx_.symbols) {   // unordered_map，顺序不确定
        if (sub_slot == info.slot_id) { source.variables.push_back(name); break; }
    }
}
```

**问题**：一个 slot 上可能坐着**多个名字**（如 `__exists_saved_N` 与其保存的原变量），
反查命中哪个取决于哈希顺序。

**修法**：让关联条目**显式携带变量名**（`Correlation` 增加 `right_var`），彻底不做反查。
这与 AGENTS.md 的 `VariableId = SlotId` 红线一致：手上有 slot 就不该回头按名字查。

## 4. Apply 与 SemiJoin 对同一 `CorrelatedSource` 的列集合需求冲突

**现象**：`PatternComprehensionApply` 需要 source 暴露 `[循环变量, 列表]`；
`SemiJoin`（嵌套 EXISTS）需要 source 暴露 `[friend, __exists_saved_N]`。
二者**共用同一个 `BoundCorrelatedSourceOp` 构造**（`bind_match.cpp:1309`），
所以任何单一列集合都无法同时满足。

**实测**：把 `__exists_*` 从 `source.variables` 过滤掉 → `PatternPredicateTwoNodes`、
`NotBarePatternExpressionInReturn`、`NestedExistsWithCorrelatedPropertyFilter` 三个测试失败。

**修法方向**：不改列集合，而是让 `PatternComprehensionApplyPhysicalOp` 按
**右子计划实际引用的 slot**（`rr.slot_layout`）决定注入集合，
`correlation` 仅作为「slot → 外层列」的查找表。

## 5. CSV loader：同名关系类型多文件时列索引越界

**现象**：导入 LDBC sf0.1 时边加载中途 `SIGABRT`：

```
stl_vector.h:1253: vector<int>::operator[]: Assertion '__n < this->size()' failed
```

**根因**：`buildEdgeTypeSchemas` 以 **edge type** 为键复用 schema，同类型的多个文件共享
`schema.properties`（各文件属性的**并集**），而 `loadOneEdgeFile` 的 `prop_cols` 来自**当前文件**表头，
逐行循环用一方索引另一方。命中 LDBC 映射里被声明多次的 5 个类型：
`HAS_CREATOR`、`HAS_TAG`、`IS_LOCATED_IN`、`REPLY_OF`、`LIKES`。

**已修**：commit `bafeaaae`（PR #203 已合并）—— 属性改为按**列名**在当前文件内解析。

**遗留**：该修法只解决越界，未验证「同名多文件属性并集」在**属性类型不一致**时是否仍正确。

## 6. 测量陷阱：CPU 频率会降到 ~33%

**现象**：本机（AMD Ryzen 7 5825U）`lscpu` 的 `CPU(s) scaling MHz` 会显示 33–34%，
此时**同一二进制**的 complex-7 比全频时慢约 2 倍（pid 933：min 8.4ms → 15.8ms）。

**教训**：跨时间比较性能前必须确认当前频率，否则会把功耗状态误判成代码回归
（本次排查中就因此误判过一次）。性能结论一律用**同二进制交错 A/B**。

## 7. 客户端长连接会被断开 —— 已修复（2026-09-26）

**现象**：单个 bolt 连接连续执行若干次后，客户端报 `Failed to read from defunct connection`；
**服务器未崩溃**（仍在监听、日志正常、内存充足）。服务端日志为
`[bolt] read error: AsyncSocketException: ReadCallback::getReadBuffer() returned empty buffer`。

**根因**：`BoltConnection::getReadBuffer()` 只在读缓冲为空时重置，但 `processMessage()`
消费完一条消息就返回，**半条消息的残留字节**让 `length()` 极少归零；而 `trimStart()`
只推进数据指针、不回收前面的空间。偏移于是前进到 64 KiB 末尾，`tailroom()` 归零，
folly 抛 `returned empty buffer` 并断连。耗尽时间只取决于**每次执行的字节数**：
实测 `RETURN 1`（约 78 B/次）第 ~840 次断、`RETURN '<1.5 KB>'`（约 1560 B/次）第 ~41 次断
（≈ 64 KiB ÷ 每次字节数）。

**修法**：`getReadBuffer()` 在 `tailroom()` 低于阈值时**回卷**缓冲区（把未消费残留搬进
新分配缓冲区）。设计与实测见 [service/neo4j-bolt-protocol.md §11](../service/neo4j-bolt-protocol.md)。

**影响（修复前）**：基准脚本需每轮新建连接，否则会把「连接断开」误判为「服务器崩溃 / 查询超时」；
官方 LDBC driver 不会为每条语句重连，因此跑到 60–100 次操作时**整个 run 被
`ServiceUnavailableException` 打断**——这是"跑完整官方 benchmark"的前置缺陷。

**验证**：`TestConnectionLifetime`（4 个用例）在修复前的二进制上失败、修复后通过；
`scripts/repro_bolt_connection.py`（直接压单/多连接，基准脚本做不到）4 并发 × 300 次全过；
官方 driver（4 线程 / 200 操作）完成且服务端零 read error。

## 8. `CREATE INDEX` 回填阶段 SIGSEGV，并留下空索引

**现象**：在 135k–287k 行的大标签上执行 `CREATE INDEX` / `CREATE UNIQUE INDEX`，服务端日志：

```
[handler] Created vertex index 'idx_msg_cd' (id=12) on Message.(creationDate)   ← 报"成功"
*** Signal 11 (SIGSEGV) received by PID 56168 (pthread TID ...) (code: address not mapped to object), stack trace: ***
```

即 **catalog 元数据先提交、回填时崩溃**，随后服务端无响应。

**后果链**（本轮实测）：
1. catalog 里留下**定义为空**的索引：`CALL db.indexes()` 能列出该索引，但它没有条目；
2. 本应走索引的点查退化为全标签扫描：
   `MATCH (m:Message {id: 3})` **946 ms**（索引完好时 4 ms）、`Post{id}` 461 ms、`Comment{id}` 495 ms；
   而小标签（≤16k 行）回填能在超时内完成，点查仍是 1 ms —— 于是故障表现为**只在大表上慢**；
3. LDBC 的短查询（short-4/5/6/7）全部是 `Message{id}` 点查，因此在坏实例上比 neo4j 慢 5–14×，
   在好实例上比 neo4j 快 52–180×。**索引状态不核对，性能结论就会整体反向**。

**loader 侧的放大因素**：
* `src/program/shell/rpc_client.cpp:47` 的 `channel->setTimeout(30000)` 被 loader 共用，
  大表回填超过 30 s 即 `TTransportException: Timed out`；
* `csv_loader.cpp:782` 对该失败**只 log warn 后继续**，`loader_main.cpp` 也不校验索引是否可用，
  于是"加载成功"的实例可能带着一堆空索引。

**待办**：
1. 查回填路径的 SIGSEGV 根因（崩溃在 `Created vertex index` 之后、异步栈上）；
2. catalog 与回填改成一个事务/两阶段：回填成功后才可见，避免留空索引；
3. loader 的 DDL 超时改为可配置（或对建索引用更长超时），并在加载结束做
   "索引存在且点查命中"的校验，失败即报错退出；
4. 重测 benchmark：任何 `MATCH (... {id: ...})` 类读数前，先证明目标标签的索引可用。
