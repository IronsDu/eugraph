# LDBC SNB Interactive SF0.1 复测（当前版本）

> **2026-09-25 重写**：整篇以**本次同机交错 A/B 实测**为唯一当前口径。
> 此前各轮（2026-09-10 / 09-19 / 09-21）的耗时表口径互不相同（数据目录、参数连接方式、
> CPU 频率都变过），留在一起只会让人误做减法，已删除；仍然有效的**根因分析与能力缺口**
> 收在第 5、6 节。各轮的完整过程记录可回溯 git。
>
> 方法要点：**每个查询新建连接**（服务端会断开长连接，见
> [known-defects-todo](../query/known-defects-todo.md) §7）、**两引擎交错执行**（本机 CPU 频率
> 会漂移，先跑完一个再跑另一个会把频率变化误读成加速）、报 min 与 p50。

## 1. 环境与口径

| 项 | eugraph | neo4j |
|---|---|---|
| 版本 | 本分支 Release（`build/release/eugraph-server`） | 5.26.30 community |
| 端点 | bolt `127.0.0.1:7688`（thrift 9090） | bolt `127.0.0.1:7687` |
| 数据 | `eugraph-sf0.1-fresh`：**330,126 点 / 1,478,002 边** | 同一份 sf0.1 导入：**330,130 点 / 1,478,003 边** |
| WiredTiger | `cache_size=256MB, eviction=(threads_max=4), transaction_sync=(enabled=true,method=fsync)` | — |
| 查询文本 | `ldbc_snb_interactive_v1_impls/cypher/queries/` **原版** | 同左（仅下述类型归一化） |
| 参数 | 文件自带 `:param` 默认值，仅 `personId`→933、`messageId`→3 | 同左，且 id 类参数按字符串传（neo4j 的 `id` 是 STRING） |
| 口径 | warmup 3 + 15 轮计时，两引擎交错；报 min / p50 | 同左 |

### 1.1 测量前对 neo4j 补齐的数据差异（否则它的读数无意义）

neo4j 实例来自**旧转换**，与 eugraph 有 5 处差异。不补齐时 complex-5/6/9/11 会返回 0 行或直接报错，
**耗时数字只是"没干活"**：

| # | 差异 | neo4j 原状 | 补齐方式 | 影响 |
|---|---|---|---|---|
| 1 | 派生标签 `Message` | 不存在 | `MATCH (n:Post) SET n:Message`（Comment 同理）→ 286,744，与 eugraph 一致 | 不补则 complex-6/9 匹配 0 行 |
| 2 | 派生标签 `Company` / `University` | 不存在 | 按 `organisation_0_0.csv` 的 `type` 列打标 → 1575 / 6380，与 eugraph 一致 | 不补则 complex-11 为 0 行 |
| 3 | `LIKES.creationDate` 为 STRING | `'1342815871582'` | 查询内 `toInteger(like.creationDate) AS likeTime` | 不补则 complex-7 抛 `TypeError` |
| 4 | `WORK_AT.workFrom` 为 STRING | `'2013'` | 查询内 `toInteger(workAt.workFrom)` | 不补则 complex-11 的 `< $workFromYear` **静默不匹配**（0 行、不报错） |
| 5 | 同 3/4 类型的历史项 | `Message.creationDate` / `HAS_MEMBER.joinDate` / `Person.birthday` | 已提前转为 INTEGER（eugraph 侧本就是 INT64） | 影响 complex-3/9/10 的日期谓词 |

**这些改写只作用在 neo4j 侧**；eugraph 侧跑的是原版文本。`scripts/bench_ldbc_ab.py --neo4j-fix-types`
即第 3、4 项的开关，第 1、2、5 项是对数据集的一次性补齐（见第 7 节）。

## 2. 同机交错 A/B（2026-09-25）

单位 ms；`ratio = neo4j_min / eugraph_min`，**> 1 表示 eugraph 更快**。

| 查询 | 行数 eg/neo | eg min | eg p50 | neo min | neo p50 | ratio |
|---|---:|---:|---:|---:|---:|---:|
| short-4 | 1 / 1 | 1.35 | 1.62 | 71.00 | 72.96 | **52.5×** |
| short-5 | 1 / 1 | 1.53 | 1.69 | 141.89 | 152.51 | **92.8×** |
| short-6 | 1 / 1 | 2.01 | 2.17 | 68.54 | 71.12 | **34.1×** |
| short-7 | 0 / 0 | 2.34 | 2.54 | 111.73 | 113.31 | **47.7×** |
| short-1 | 1 / 1 | 1.11 | 1.31 | 1.32 | 1.50 | 1.19× |
| short-3 | 3 / 3 | 1.47 | 1.63 | 1.60 | 1.76 | 1.09× |
| short-2 | 10 / 10 | 4.49 | 5.24 | 2.30 | 2.61 | 0.51× |
| complex-8 | 20 / 20 | 6.51 | 7.05 | 2.72 | 2.90 | 0.42× |
| complex-7 | 7 / 7 | 6.92 | 7.51 | 2.64 | 2.90 | 0.38× |
| complex-11 | 4 / 4 | 9.95 | 10.57 | 3.14 | 3.56 | 0.32× |
| complex-4 | 0 / 0 | 13.45 | 14.15 | 3.22 | 3.56 | 0.24× |
| complex-2 | 20 / 20 | 22.05 | 23.06 | 3.78 | 4.06 | 0.17× |
| complex-10 | 10 / 10 | 67.21 | 71.71 | 7.56 | 8.54 | 0.11× |
| **complex-12** | 3 / 3 | **459.26** | 476.47 | 91.80 | 100.65 | **0.20×** |
| **complex-3** | 0 / 0 | **527.80** | 538.55 | 24.70 | 26.02 | **0.05×** |

**行数两引擎全部一致**——这是本表能作为对照的前提。此前几轮出现过的 0 行 / 行数不符，
全部由 §1.1 的数据差异与参数口径造成，与查询语义无关。

### 2.1 读法

* **按 id 的索引点查/一跳查询，eugraph 明显更快**：`short-4/5/6/7` 快 **34–93×**
  （neo4j 这三条要 68–142 ms，eugraph 1.3–2.3 ms），`short-1/3` 也略快。
* **最大差距是 complex-3（21×）与 complex-12（4.6×）**，且都在数百 ms 量级：
  - complex-3：`Tag(name) → Post → HAS_CREATOR → Country` 的两国计数，553 ms vs 25 ms；
  - complex-12：`TagClass(name) → Tag → Post → HAS_CREATOR → Person` 的回复计数，459 ms vs 92 ms。
  两者都是"**从一个具名起点沿关系多次跳转 + 中间结果放大**"的形状，落在展开顺序与中间结果规模上，
  与值拷贝无关（那一层的收益已在早前轮次落地）。
* complex-10 本轮 **67 ms**（neo4j 8.5 ms，8.8×）。早前轮次记录过 7.1×，两者同档；
  机器频率与环境不同，**不宜跨会话相减**。

## 2.2 用官方加载口径重导数据（消除 §1.1 那些差异的根因）

§1.1 里对 neo4j 补的 5 处差异，根因**不是数据，而是加载时用错了 CSV 表头语义**。官方
Neo4j 参考实现并不直接 `neo4j-admin import` 原始 CSV，而是先跑
`cypher/scripts/convert-csvs.sh`：

1. 用 `cypher/scripts/headers.txt` 里的**类型化表头覆盖**每个文件的原始表头。
   该文件每行是 `文件 表头`，冒号后就是类型：`id:ID(Organisation)`（主键与其 id 空间）、
   `:START_ID` / `:END_ID`（关系两端）、**:`LABEL`（这一列的值是标签名）**、
   `name:STRING` / `creationDate:LONG` / `workFrom:INT`（属性类型）。
2. 再用 sed 把标签列的值改成标签拼写：`|company|` → `|Company|`、`|city$|` → `|City|` 等。

**`headers.txt` 是官方 pipeline 的数据准备输入，不是 `neo4j-admin import` 的运行时输入**——
但缺了它就会丢 schema：原始 CSV 里 `type` 只是一列普通属性，于是 `Company` / `University` /
`City` / `Country` / `Continent` 这些标签根本不存在（complex-11 因此匹配 0 行），
`workFrom` / `creationDate` 的类型也没有保证。

我们的 loader **支持与 `neo4j-admin import` 同形的映射**（`--nodes=Label[:Label...]=file`、
`--relationships=TYPE=file`，并识别 `:LABEL` 列），所以可以完全按官方口径加载：

```bash
# 等价于官方 convert-csvs.sh + import-to-neo4j.sh（headers.txt 作为唯一事实来源）
python3 scripts/prepare_ldbc_official_csv.py \
    --src /home/dodo/code/fuck/ldbc-conv --out /tmp/ldbc-official \
    --load --host 127.0.0.1 --port 9090
```

**实测结果**（sf0.1，全新实例）：Message 286,744 / Post 135,701 / Comment 151,043 /
Person 1,528 / Organisation 7,955 / **Company 1,575** / **University 6,380** /
**City 1,343 / Country 111 / Continent 6** / Tag 16,080 / TagClass 71 / Forum 13,750，
且 15 类关系计数与原始 CSV 直导**逐项相同**——即官方口径下**不再需要任何手工补丁**。

> 两个 Vertex 文件的第二个标签（`Comment:Message`、`Post:Message`）来自
> `import-to-neo4j.sh` 而非 `headers.txt`，脚本里以 `EXTRA_NODE_LABELS` 显式记录。

## 2.3 官方 driver 端到端跑通（2026-09-25，进行中）

比自写 A/B 脚本更权威的做法：**用 LDBC 官方 driver + 官方 substitution parameters** 跑。
本轮已把这条链路打通，要素如下（复现命令见 §7.1）：

| 环节 | 结论 |
|---|---|
| 官方 driver jar | 本机无 Maven/mvnw/`~/.m2`，已下载 Maven 3.9.16 并用 `mvn -q clean package -DskipTests -Pcypher` 构建出 `cypher-1.2.0-SNAPSHOT.jar` |
| Java 兼容性 | 上游要求 Java 11，本机 JDK 25 需加 `--add-exports java.base/sun.nio.ch=ALL-UNNAMED --add-opens java.base/java.nio=ALL-UNNAMED`（SBE 编解码器的 `DirectBuffer`），否则 `IllegalAccessError` |
| **Bolt 兼容性** | **官方使用的 `neo4j-java-driver 4.4.3` 可直连 eugraph**（BASIC 认证，密码固定 `eugraph`；参数化查询、long 往返、列表参数均正常） |
| 官方参数 | `datasets.ldbcouncil.org/snb-interactive-v1-parameters/substitution_parameters-sf0.1.tar.zst`，**与本库 sf0.1 数据同一 id 空间**（如 personId=30786325579101 在我们库里存在） |
| 数据加载 | §2.2 的 `prepare_ldbc_official_csv.py`；关系类型必须显式给大写名（见该脚本 `REL_TYPE`） |
| 审计 | 官方 `PASSED SCHEDULE AUDIT` 已通过（complex-5 关闭时，1 线程 / 250 操作：吞吐 1.89 op/s） |

### 2.3.1 官方口径下的实测延迟（warmup 阶段，1 线程）

> ⚠️ **下表取自后来崩溃的实例**（索引不完整，详见 2.3.2 第 0/1 条），**仅保留作为过程记录**，
> 不代表引擎当前水平；其中 short-4/5/6/7 的数值已被 2.3.3 的修正值取代
> （0.8–1.9 ms vs 下表 546–1955 ms）。复杂查询部分尚待"索引完好实例"上重测。

| 官方查询 | 次数 | 均值 ms | min | max |
|---|---:|---:|---:|---:|
| LdbcShortQuery4MessageContent | 7 | 1954.7 | 1928 | 1995 |
| LdbcQuery3 | 1 | 1663.0 | 1663 | 1663 |
| LdbcQuery12 | 1 | 568.0 | 568 | 568 |
| LdbcShortQuery6MessageForum | 7 | 558.1 | 535 | 626 |
| LdbcShortQuery7MessageReplies | 7 | 553.4 | 544 | 584 |
| LdbcShortQuery5MessageCreator | 7 | 545.9 | 543 | 548 |
| LdbcQuery6 | 1 | 524.0 | 524 | 524 |
| LdbcQuery2 | 1 | 211.0 | 211 | 211 |
| LdbcQuery10 | 1 | 201.0 | 201 | 201 |
| LdbcShortQuery2PersonPosts | 7 | 7.1 | 6 | 8 |
| LdbcShortQuery3PersonFriends | 7 | 5.1 | 3 | 10 |
| LdbcShortQuery1PersonProfile | 7 | 2.4 | 1 | 5 |

> 与 §2 的差异说明：§2 用的是**文件自带 `:param` 默认值 + 我选定的 messageId**，
> 本节用**官方 substitution parameters**（16 组随机参数）与官方 driver 的调度器。
> 两者都是有效读数，但**不可互相加减**；例如 `short-4` 在 §2 是 1.35 ms（messageId=3），
> 本节是 1954.7 ms（官方参数指向的消息更大/更热）。**官方口径才是对外可比的数字。**

### 2.3.2 官方口径暴露的问题

> ⚠️ **本节最重要的一条是第 0 条**：先前的"short 查询比 neo4j 慢 5–14×"结论是**错的**，
> 它测在一个索引失效（且随后崩溃）的实例上。修正后的结论见 2.3.3。

0. **索引失效会让点查慢 80–290×，并伪装成"引擎退步"**
   `CREATE INDEX` 在大标签上**先写 catalog 元数据、再回填**；回填过程中服务器会
   崩溃（见第 1 条），于是 catalog 里留下**没有内容的索引**。此时本应是索引点查的语句
   退化为全标签扫描：

   | 语句 | 索引失效的实例 | 索引完好的实例 |
   |---|---:|---:|
   | `MATCH (m:Message {id: 3}) RETURN m.id` | 946 ms | **4 ms** |
   | `MATCH (m:Post {id: 3}) RETURN m.id` | 461 ms | 索引点查 |
   | `MATCH (c:Comment {id: 102205})` | 495 ms | 索引点查 |
   | `MATCH (p:Person {id: 933})`（小表，回填成功） | 1 ms | 1 ms |

   loader 对此**只 warn 不报错**（`Failed to create unique index ...: TTransportException:
   Timed out`），于是"加载成功"的实例可能带着一堆空索引，后续所有读数都是废的。
   **复现前必须核对**：`CALL db.indexes()` 列出索引 **且** 该标签的点查是毫秒级。
1. **`CREATE INDEX` 回填阶段会 SIGSEGV 打死服务端**（新发现，P0）
   在 135k–287k 行的大标签上建索引，服务端日志顺序为：

   ```
   [handler] Created vertex index 'idx_msg_cd' (id=12) on Message.(creationDate)   ← 报"成功"
   *** Signal 11 (SIGSEGV) received by PID 56168 ... stack trace: ***
   ```

   即 **catalog 先提交、回填时崩溃**：索引定义留在库里、内容为空（第 0 条的直接成因），
   服务端随后无响应。官方 schema 那批读数正是在这个崩溃后的实例上取的。
   > 注：小标签（≤16k 行）回填可以在 loader 的 30 s RPC 超时内完成，所以只有大表暴露此问题。
2. **loader 的索引创建用 30 s 超时，失败只 warn**
   `src/program/shell/rpc_client.cpp:47` 的 `channel->setTimeout(30000)` 被 loader 共用；
   大表回填超 30 s 即报 `TTransportException: Timed out`，而 `csv_loader.cpp:782` 只 log warn
   后继续。**加载流程缺少"索引是否真的可用"的校验**。
3. **complex-5 在官方参数下挂死**
   单线程直接跑 `interactive-complex-5.cypher`（personId=15393162790207, minDate=1344643200000）
   **超过 400 s 不返回**，服务端多个 compute 线程持续满载。官方 driver 跑到该查询时整个 run
   停滞。§5.2 记录的 join order / `IN` 下推问题在官方参数下更容易命中，P0。
4. **长连接被断开会把官方 driver 打挂**
   跑到 `LdbcShortQuery2PersonPosts` 时客户端报
   `ServiceUnavailableException: Connection to the database failed` 并终止整个 run；
   同一时刻服务端日志有 `[bolt] read error: ... returned empty buffer` 与
   `query cancelled mid-stream`。这是 [known-defects-todo](../query/known-defects-todo.md) §7
   的已知缺陷——自写脚本靠"每轮新建连接"绕过，**官方 driver 不会绕**，修掉它是跑完整
   官方 benchmark 的前置条件。

### 2.3.3 修正：short 查询仍显著快于 neo4j

在**索引完好**的实例（`sf0.1-fresh`，`CALL db.indexes()` 齐全且点查毫秒级）上重测，
用与官方 driver 同类的随机 messageId，取 3 次最小值：

| 查询 | messageId | eugraph | neo4j | 倍数 |
|---|---:|---:|---:|---:|
| short-4 | 3 | **0.8 ms** | 78.4 ms | 98× |
| short-4 | 618475290625 | **1.0 ms** | 76.8 ms | 77× |
| short-5 | 3 | **0.9 ms** | 162.2 ms | 180× |
| short-6 | 618475290625 | **1.5 ms** | 78.3 ms | 52× |
| short-7 | 618475290625 | **1.9 ms** | 122.1 ms | 64× |

即 §2 的"short-4/5/6/7 快 34–93×"结论**成立**；2.3.1 表里那组"slow"数字以及据此得出的
"引擎在 Message{id} 上退步"的判断**作废**，它们全部来自空索引实例。同时可见 eugraph
的**延迟与 messageId 无关**（0.8–1.9 ms 恒定），而 neo4j 在同一批参数上波动到 77–162 ms。

> **教训**：索引状态是这类读数的一等前提。任何 LdbcShortQuery4/5/6/7 与
> `MATCH (... {id: ...})` 的对比，都必须先证明目标标签的索引真的可用
> （`CALL db.indexes()` + 一次毫秒级点查），否则测的是全表扫描。

## 3. 未纳入对照的查询

| 查询 | 原因 |
|---|---|
| complex-1 / complex-13 / complex-14 | **语法不支持**：依赖 `shortestPath` / `allShortestPaths` + `reduce`（eugraph 报 `UnexpectedSyntax`） |
| complex-5 | **eugraph 不可用**：实测 3 个 compute 线程连续满载 9 分钟不返回，客户端 120 s 超时也不生效（阻断算子只在批边界检查取消）；neo4j 92 ms 完成 |
| complex-6 / complex-9 | 早前轮次 eugraph 可执行（0.49 s / 3.96 s）；本轮为控制耗时未跑，neo4j 侧为 31 / 37 ms |

## 4. 测量陷阱（复现前务必先读）

1. **CPU 频率会漂**：同一二进制跨会话可差约 2 倍。比较前先看 `grep MHz /proc/cpuinfo`，尽量同机交错。
2. **长连接会被断开**：连续执行数十次后服务端会断开 bolt 连接（**服务器未崩**），
   复用连接池会把它记成"查询失败/变慢"。基准脚本必须每个查询新建连接。
3. **neo4j 侧缺 5 处数据差异**（§1.1）。**不补齐就测 neo4j，拿到的是假数字**——
   它会给 0 行结果一个漂亮的毫秒级成绩。
4. **id 类型不同**：eugraph 是 INTEGER、neo4j 是 STRING，id 类参数要按引擎转型，
   否则查询静默返回 0 行。

## 5. 能力缺口与遗留问题（仍然有效）

### 5.1 语法能力缺口

| 语句 | 缺口 | 方案 |
|------|------|------|
| complex-1 | `shortestPath` 不支持 | 实现双向 BFS shortest-path 算子 |
| complex-13 | 同上（现用 `knows*1..3 + min(length(path))` 近似） | 同上 |
| complex-14 | `allShortestPaths` + `reduce` 不支持 | 实现 all-shortest-paths + reduce |

### 5.2 complex-5：CPU 不收敛（当前最大问题）

`OPTIONAL MATCH (friend)<-[:HAS_CREATOR]-(post)<-[:CONTAINER_OF]-(forum) WHERE friend IN friends`
不返回。早前记录的判断是"需查 join order / `IN` 谓词下推，与内存无关"；本轮在 Release 下实测
**3 个 compute 线程连续 9 分钟满载**，且只有客户端断开后算子才停止。两条下一步：
① 该形状的 join order 与 `IN` 下推；② 查清取消检查为何在阻断算子内部不生效。

### 5.3 仍然较慢的语句

| 查询 | 差距 | 方向 |
|---|---|---|
| complex-3 | 21× | `Tag(name)` 起点选择、`HAS_CREATOR` 方向的前向/反向表选择 |
| complex-12 | 4.6× | `TagClass(name) → Tag → Post` 多跳展开与中间结果规模 |
| complex-10 | 8.8× | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 |

## 6. 早前轮次已修复并验证的问题（结论保留）

1. **Q12 计划重写**：`Apply(collect(tag.id), HashJoin(friend))`，左支从 `Tag(id) IN tags` 反向展开。
2. **Expand 批处理**：dst label 一次批量检查；新增 `scanEdgesBatch` 快速路径。
3. **VarLenExpand 邻接缓存 + OR 索引剪枝**：同 chunk 内复用邻接与 label 判定；`src.prop = v OR dst.prop = v` 下推为索引允许集。
4. **Complex-3 索引优先 join**：日期谓词先拆为独立 Filter 再下推，改走 `HashJoin(候选朋友, IndexScan(creationDate 区间))`。
5. **Short-7 OPTIONAL MATCH 相关性链修复**：起点与终点都已绑定时仍走 `CorrelatedSource → Expand → Expand(bound_endpoint)`，不退化为独立扫描 + CrossProduct。
6. **Complex-7 原版 pattern expression 支持**：`NOT` / `AND` / `OR` / `CASE WHEN` / `ANY` / 列表推导 / `EXISTS { pattern }` 等布尔上下文中的 bare pattern 解析为 `ExistsExpr`。
7. **重型值类型句柄化**：`VertexValue` / `EdgeValue` / `ListValue` / `MapValue` / `PathValue` 以 `shared_ptr` 承载、`ColumnBuffer` 存句柄；分配占比 80% → 11%（剖析见 [query-value-copy-profiling](query-value-copy-profiling.md)）。

## 7. 复现命令

### 7.1 用官方口径加载数据（推荐，见 §2.2）

```bash
# headers.txt 为唯一事实来源：覆盖类型化表头 + 把标签列值改成标签名，然后加载
python3 scripts/prepare_ldbc_official_csv.py \
    --src /home/dodo/code/fuck/ldbc-conv --out /tmp/ldbc-official \
    --load --host 127.0.0.1 --port 9090
```

加载完成后**重启实例**（loader 建的索引只写入 catalog，运行中的实例内存里没有；
未重启时 `MATCH (t:Tag {name:'Shakira'})` 需 51630 ms，重启后 45 ms）。

### 7.2 官方 driver（§2.3）

```bash
# ① 构建官方 driver（首次；需联网拉依赖）
MAVEN=/tmp/vbench/apache-maven-3.9.16/bin/mvn
( cd /home/dodo/code/fuck/ldbc_snb_interactive_v1_impls && $MAVEN -q clean package -DskipTests -Pcypher )
JAR=/home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/target/cypher-1.2.0-SNAPSHOT.jar

# ② 官方 substitution parameters（与 sf0.1 数据同一 id 空间）
curl -sSL -o sf01-params.tar.zst \
  https://datasets.ldbcouncil.org/snb-interactive-v1-parameters/substitution_parameters-sf0.1.tar.zst
mkdir -p sf01-params && tar --zstd -xf sf01-params.tar.zst -C sf01-params

# ③ benchmark.properties 关键项（其余沿用官方默认 driver/benchmark.properties）
#    endpoint=bolt://localhost:7692     ← eugraph 实例
#    user=neo4j  password=eugraph       ← eugraph 要求 BASIC 认证
#    queryDir=queries/                  ← 指向 cypher/queries 的副本
#    ldbc.snb.interactive.scale_factor=0.1
#    ldbc.snb.interactive.parameters_dir=<上一步目录>
#    ldbc.snb.interactive.LdbcQuery{1,13,14}_enable=false   ← shortestPath 语法不支持
#    ldbc.snb.interactive.LdbcUpdate*_enable=false          ← 只跑读（等价官方 disable-updates.sh）
#    ldbc.snb.interactive.LdbcQuery5_enable=false           ← 见 §2.3.2 第 1 条，否则 run 停滞

# ④ 运行（JDK 25 需要模块导出；JDK 11 不需要）
java --add-exports java.base/sun.nio.ch=ALL-UNNAMED --add-opens java.base/java.nio=ALL-UNNAMED \
  -cp $JAR org.ldbcouncil.snb.driver.Client -P benchmark.properties
```

结果落在 `results/LDBC-SNB-results.json`（per-query 均值/min/max/分位）与
`results/LDBC-SNB-validation.json`（官方 schedule audit）。

**正常噪声**：driver 会打印 `Unable to load query from file: ...-duration-as-function.cypher`
等——那是其余 workload 变体的文件缺失。

### 7.3 自写同机交错 A/B（§2）

```bash
# eugraph 侧库名 default，neo4j 侧 neo4j；每查询新建连接
python3 scripts/bench_ldbc_ab.py \
    --queries-dir /home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/queries \
    --rounds 15 --warmup 3 --neo4j-fix-types \
    --skip complex-1,complex-5,complex-6,complex-9,complex-13,complex-14

# 只测 eugraph 单侧
python3 scripts/bench_ldbc_interactive.py \
    --queries-dir /home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/queries \
    --uri bolt://127.0.0.1:7688 --person-id 933 --override messageId=3 \
    --warmup 1 --iters 3 --skip complex-1,complex-5,complex-6,complex-9,complex-13,complex-14
```

两个自写脚本的参数解析都支持 LDBC 查询文件的两种写法（`:param [{...}] => {...}` 与
`:param name: value`）；`--override key=value` 用于把参数换成**本数据集里真实存在**的取值。

---

## 8. 本轮发现的问题汇总（2026-09-25）

> 分类记录本轮为跑通"官方口径对比"而暴露的问题。**A 类是引擎缺陷（待修），B 类是数据加载
> 与工具链问题，C 类是并发/长时间运行暴露的问题，D 类是测量方法本身的陷阱**——D 类里有几条
> 是**我本轮自己犯的错**，一并留下以免后来人重踩。

### A. 引擎缺陷（待修）

| # | 问题 | 证据 | 影响 | 状态 |
|---|---|---|---|---|
| A1 | **`CREATE INDEX` 回填阶段 SIGSEGV，服务端崩溃** | 大标签建索引时日志为 `Created vertex index 'idx_msg_cd' (id=12) on Message.(creationDate)` 紧接 `*** Signal 11 (SIGSEGV) ... ***` | catalog 先提交、回填崩溃 → 留下**空索引**；服务端随后无响应 | 未修，见 [known-defects-todo §8](../query/known-defects-todo.md) |
| A2 | **空索引不报错，点查静默退化为全标签扫描** | `Message{id}` 946 ms（索引完好 4 ms）、`Post{id}` 461 ms、`Comment{id}` 495 ms；小标签因回填能完成仍是 1 ms | 症状是"**只在大表上慢**"；所有 `MATCH (... {id: ...})` 类读数可能整体反向 | 未修（与 A1 同源，需 catalog/回填两阶段化） |
| A3 | **loader 的 DDL 用 30 s 超时且失败只 warn** | `src/program/shell/rpc_client.cpp:47` `channel->setTimeout(30000)`；`csv_loader.cpp:782` 仅 `spdlog::warn` | "加载成功"的实例可能带着一堆空索引，加载流程结束也不校验索引可用性 | 未修 |
| A4 | **complex-5 在官方参数下挂死** | personId=15393162790207 / minDate=1344643200000，单线程 **>400 s 不返回**，多个 compute 线程持续满载；官方 driver 跑到该查询整个 run 停滞 | 官方 benchmark 无法完整跑完 | 未修，见 §5.2 |
| A5 | **规划器对多标签扫描拒绝用索引** | `physical_planner.cpp:2487`：`// Index scan only when single label (multi-label requires runtime intersection)` + `if (scan_op.label_ids.size() == 1)` | `Message`（`Post:Message`/`Comment:Message` 超类）上的属性点查永远不走索引 | 未修（设计取舍，需运行期标签交集校验后才能放开） |

### B. 数据加载与工具链问题

| # | 问题 | 证据 | 结论 |
|---|---|---|---|
| B1 | **loader 建完索引后不重启实例，索引不生效** | 同一查询在"加载后未重启"为 **51630 ms**，重启后 **45 ms** | 索引只写入 catalog，运行中的实例内存里没有。**加载后必须重启** |
| B2 | **关系类型必须显式指定，否则官方查询全部匹配不到** | `--relationships=person_knows_person=file` 会把类型存成 `person_knows_person`；`MATCH ()-[r:KNOWS]->()` 返回 **0**，而 loader 日志写着 `Loaded 14073 edges for 'person_knows_person'` | **边计数正确、类型名错误**，极其隐蔽；已在 `prepare_ldbc_official_csv.py` 的 `REL_TYPE` 表中显式给出大写关系名 |
| B3 | **原始 CSV 表头缺类型化信息，会丢 schema** | 官方 pipeline 先用 `headers.txt` 覆盖表头再用 sed 改标签值；直接导原始 CSV 时 `Company`/`University`/`City`/`Country`/`Continent` 全部为 0（`type` 只是一列普通属性），complex-11 因此 0 行 | 已提供 `scripts/prepare_ldbc_official_csv.py`（见 §2.2），官方口径下无需任何手工补丁 |
| B4 | **neo4j 侧旧转换的类型/标签差异会让对照失真** | `LIKES.creationDate` 为 `'1342815871582'`（complex-7 直接抛 `TypeError`）；`WORK_AT.workFrom` 为 `'2013'`（与 int 比较**静默不匹配 → 0 行**）；缺 `Message`/`Company`/`University` 派生标签 | 对照前必须先补齐（§1.1）；**"0 行"既可能是引擎错，也可能是对方数据缺**，不核验就会误判 |
| B5 | **两引擎 id 类型不同，参数传错静默 0 行** | eugraph `id` 是 INTEGER（`933`），neo4j 是 STRING（`'933'`） | 跨引擎脚本必须按引擎转型；本项目对照脚本因此有 `id_type` 参数 |

### C. 并发与长时间运行暴露的问题

| # | 问题 | 证据 | 结论 |
|---|---|---|---|
| C1 | **长连接会被服务端断开，并会打挂官方 driver** | 跑到 `LdbcShortQuery2PersonPosts` 时客户端 `ServiceUnavailableException: Connection to the database failed` 并终止整个 run；同一时刻服务端有 `[bolt] read error: ... returned empty buffer` 与 `query cancelled mid-stream` | 即 [known-defects-todo §7](../query/known-defects-todo.md)；自写脚本靠"每查询新建连接"绕过，**官方 driver 不会绕**，是跑完整官方 benchmark 的前置条件 |
| C2 | **并发下点查延迟显著抬高** | 4 并发 short-7：单线程 550 ms → 并发每个 3.2 s（≈6×） | 官方 driver 默认多线程，读到的延迟会高于单线程直测；对比两引擎时必须同线程数 |
| C3 | **客户端断开后，服务端算子会继续算** | complex-5 在客户端 120 s 超时断开后仍持续满载；另一例 `query cancelled mid-stream; rolling back after 0 record(s)` 才停 | 取消只在批边界生效，阻断算子内部不检查；跑基准时要留够超时，否则残留计算会污染后续读数 |

### D. 测量陷阱（含本轮我自己的失误）

| # | 陷阱 | 本轮实例 | 教训 |
|---|---|---|---|
| D1 | **在索引失效的实例上测"引擎快慢"** | 我据此得出"short-4/5/6/7 比 neo4j 慢 5–14×"，并在文档里写下"引擎在 `Message{id}` 上退步"——**结论完全错误**；修正后是**快 52–180×**（§2.3.3） | 任何 id 点查类读数前，**先证明索引可用**（`CALL db.indexes()` + 一次毫秒级点查） |
| D2 | **忘了"加载后重启实例"** | 未重启时 `Tag{name}` 点查 51630 ms，重启后 45 ms（1148×） | 见 B1；把"没重启"误判成引擎慢 1000 倍 |
| D3 | **官方参数 vs 自选参数混用导致读数不可比** | §2（自选 messageId）与 §2.3（官方 16 组随机参数）数字差 40× 以上；我还据此怀疑过引擎 | 两套口径都有效，但**不可互相加减**；对外引用必须注明参数来源 |
| D4 | **`--skip`/匹配用了过宽的字符串** | 用 `complex-1.` 只跳到 `complex-1` 之外还漏跳过；`pgrep -f bench_xxx` 匹配到**自己的命令行**，导致自杀式 kill（本轮发生 3 次，误杀了 7688/9091 实例） | 匹配要用词边界；杀进程用 `ps -eo pid,comm` 过滤 comm 而不是 `pgrep -f` |
| D5 | **端口/实例对应关系搞错** | 曾把官方 driver 指向"原始 CSV 加载、无官方 schema"的实例，读数 0.02 op/s，差点误判为引擎极慢 | 多实例并行时，把"端口 ↔ 数据目录 ↔ schema 状态"记在纸面上再测 |
| D6 | **CPU 频率漂移与跨会话比较** | 本机同一二进制跨会话可差约 2× | 性能结论必须同机交错 A/B；绝对值只在注明条件下引用 |
| D7 | **跨引擎"0 行/行数不符"未必是缺陷** | 多次遇到 eugraph 0 行 vs neo4j N 行，最终查明分别是：neo4j 缺派生标签（B4）、neo4j 字段为字符串（B4）、我的脚本关系类型写错（B2）、文件默认参数指向不存在的 id | 先核**双方输入是否等价**，再判引擎对错 |
