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

### 7.0 用官方口径加载数据（推荐，见 §2.2）

```bash
# headers.txt 为唯一事实来源：覆盖类型化表头 + 把标签列值改成标签名，然后加载
python3 scripts/prepare_ldbc_official_csv.py \
    --src /home/dodo/code/fuck/ldbc-conv --out /tmp/ldbc-official \
    --load --host 127.0.0.1 --port 9090
```


```bash
# 1) 一次性补齐 neo4j 的派生标签（类型差异由 --neo4j-fix-types 在查询内处理）
python3 - <<'PY'
import csv
from neo4j import GraphDatabase
rows = list(csv.DictReader(open('/home/dodo/code/fuck/ldbc-conv/static/organisation_0_0.csv'), delimiter='|'))
g = {}
for r in rows:
    g.setdefault(r['type'], []).append(r['id:ID(Organisation)'])
d = GraphDatabase.driver("bolt://127.0.0.1:7687", auth=None)
with d.session(database="neo4j") as s:
    for t, lbl in (('company', 'Company'), ('university', 'University')):
        for i in range(0, len(g[t]), 1000):
            s.run(f"MATCH (o:Organisation) WHERE o.id IN $ids SET o:{lbl}", ids=g[t][i:i+1000])
PY

# 2) 起 eugraph Release（数据目录用 sf0.1-fresh）
./build/release/eugraph-server --thrift-port 9090 --bolt-port 7688 \
    --data-dir /home/dodo/code/fuck/eugraph-sf0.1-fresh

# 3) 同机交错 A/B（每个查询新建连接；eugraph 侧库名 default，neo4j 侧 neo4j）
python3 scripts/bench_ldbc_ab.py \
    --queries-dir /home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/queries \
    --rounds 15 --warmup 3 --neo4j-fix-types \
    --skip complex-1,complex-5,complex-6,complex-9,complex-13,complex-14

# 4) 只测 eugraph 单侧
python3 scripts/bench_ldbc_interactive.py \
    --queries-dir /home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/queries \
    --uri bolt://127.0.0.1:7688 --person-id 933 --override messageId=3 \
    --warmup 1 --iters 3 --skip complex-1,complex-5,complex-6,complex-9,complex-13,complex-14
```

两个脚本的参数解析都支持 LDBC 查询文件的两种写法（`:param [{...}] => {...}` 与 `:param name: value`）；
`--override key=value` 用于把参数换成**本数据集里真实存在**的取值（文件默认的 `personId`/`messageId`
往往指向大数据集的 id，在 sf0.1 里不存在，会让查询静默返回 0 行）。
