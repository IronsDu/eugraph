# LDBC SNB Interactive SF0.1 复测（当前版本）

> **2026-09-25 增补**：§1.0 记录"CSV 三种状态与类型从哪来"（官方原版 / ldbc-conv /
> headers.txt 类型化版，以及 neo4j 与我们的 loader 各自如何定类型）；§8 汇总本轮发现的问题
> （引擎缺陷 A / 加载与工具链 B / loader 专项 B2 / 并发 C / 测量陷阱 D，含我自己的失误）。
>
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

### 1.0 CSV 的三种状态与"类型从哪来"（反复踩的点，先看这个）

LDBC 的 CSV 到能用之间隔着一步**表头类型化**，官方原版、部分转换版、类型化版三者不同：

| 状态 | 路径 / 来源 | 表头长什么样 | 有类型吗 |
|---|---|---|---|
| ① **官方原版** | LDBC SURF 下载包 `social_network-sf0.1-CsvBasic-LongDateFormatter` | `id\|type\|name\|url`；关系表 `Person.id\|Organisation.id\|workFrom` | **完全没有** |
| ② **部分转换版** | `/home/dodo/code/fuck/ldbc-conv`（本仓一直用它加载） | `id:ID(Organisation)\|type\|name\|url`；`:START_ID(Person)\|:END_ID(Organisation)\|workFrom` | **只有 id 列注解，属性列无类型**；无 `:LABEL`；`type` 值仍是小写 `city`/`company` |
| ③ **类型化版** | `cypher/scripts/headers.txt` 覆盖后的产物（本仓由 `scripts/prepare_ldbc_official_csv.py` 生成） | `id:ID(Organisation)\|:LABEL\|name:STRING\|url:STRING`；`:START_ID(Person)\|:END_ID(Organisation)\|workFrom:INT` | **全齐**：`birthday:LONG` `creationDate:LONG` `length:INT` `workFrom:INT` `classYear:INT` `speaks:STRING[]` `email:STRING[]`… |

类型定义**全部**在 `cypher/scripts/headers.txt`（31 行，格式 `文件 表头`）。官方流程是：

```
① 原版 CSV（无类型）
   │  cypher/scripts/convert-csvs.sh：
   │    ① 用 headers.txt 的类型化表头覆盖每个文件的表头
   │    ② sed 把标签列的值改成标签拼写（|city$|→|City|、|company|→|Company|…）
   ▼
③ 类型化 CSV
   │  neo4j-admin import / 我们的 loader
   ▼
入库：类型为 LONG/INT，标签为 Company/University/City/Country/Continent
```

**关键点**：`neo4j-admin import` **不读 `headers.txt`**——它读的是被覆盖后的 CSV 表头。
`headers.txt` 是**加载前的数据准备输入**。`convert-csvs.sh:25-37` 就是那段覆盖逻辑
（`echo ${HEADER} | cat - <(tail -n +2 file)`），第 40-44 行是标签值的 sed。

**neo4j 如何定类型**（源码：`community/import-util/.../csv/DataFactories.java`）：
列头按 `name:type` 解析 → `extractors.valueOf(typeSpec)` 在 `Extractors` 注册表里查名字
（查不到直接抛 `'xxx' is not a valid type.`）。可用类型名：`String` `Long` `Int` `Char`
`Short` `Byte` `Boolean` `Double` `Float` 及各自 `*Array`，加 `Point`/`Date`/`Time`/
`DateTime`/`LocalTime`/`LocalDateTime`/`Duration`（及数组）。
**没有冒号的列 = 属性，类型默认 `String`**；`:ID(...)`/`:START_ID(...)`/`:END_ID(...)`
的解析由命令行 `--id-type=INTEGER|STRING|ACTUAL` 决定；`:LABEL`/`:TYPE`/`:IGNORE`/`:ACTION`
是结构性类型、不产生属性值。

**与我们 loader 的差异**（能力等价，差别在"表头没写类型时"）：

| 维度 | neo4j-admin import | 我们的 loader |
|---|---|---|
| 类型来源 | `name:type`，**无则 String** | `name:type`（`:INT`/`:LONG`/`:STRING`…），**无则按值推断** |
| id 列 | `:ID(Group)` + `--id-type` | `id:ID(Group)`，固定 INT64 |
| 标签 | `:LABEL` 列 / `--nodes=Label:Label` | `:LABEL` 列（`HeaderKind::LABEL`）/ `--nodes=Label:Label`（`:` 分隔） |
| 关系类型 | `--relationships=TYPE=file` | 同左（**必须显式给**，否则用文件名推导出的类型名，见 §8 B2） |
| 类型不符 | 报错退出 | 按推断处理 |

**为什么这件事反复咬人**：直接导 ②（ldbc-conv）会同时丢两样东西——
`Company`/`University`/`City`/`Country`/`Continent` 标签（没有 `:LABEL` 列）与属性类型
（`workFrom` 等无声明）。前者让 complex-11 返回 0 行，后者让两引擎在日期/数值谓词上分叉
（neo4j 旧实例里 `WORK_AT.workFrom` 是 `'2013'`、`LIKES.creationDate` 是字符串，见 §1.1）。
**必须用 ③ 加载**（`prepare_ldbc_official_csv.py`），否则两边永远对不齐。

> 想看 ③ 长什么样：`/tmp/vbench/conv4/`（本仓生成，注意 /tmp 会丢，可自行重新生成）；
> 想看官方原生实现：设好 `NEO4J_VANILLA_CSV_DIR` / `NEO4J_CONVERTED_CSV_DIR` 后跑
> `cypher/scripts/convert-csvs.sh`（本机这两个变量未设，故官方产物不存在）。

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
`vs neo4j` 列给出 eugraph 相对 neo4j 的倍率（`neo_min / eg_min`），颜色语义沿用本文件历史口径：

> 🟢 = 性能相当或更快（eugraph 慢 ≤ 1.5×）｜🟡 = 慢 1.5–5×｜🔴 = 慢 5× 以上

| 查询 | 行数 eg/neo | eg min | eg p50 | neo min | neo p50 | ratio | vs neo4j |
|---|---:|---:|---:|---:|---:|---:|:---:|
| short-5 | 1 / 1 | 1.53 | 1.69 | 141.89 | 152.51 | **92.8×** | 🟢 **快 93×** |
| short-4 | 1 / 1 | 1.35 | 1.62 | 71.00 | 72.96 | **52.5×** | 🟢 **快 53×** |
| short-7 | 0 / 0 | 2.34 | 2.54 | 111.73 | 113.31 | **47.7×** | 🟢 **快 48×** |
| short-6 | 1 / 1 | 2.01 | 2.17 | 68.54 | 71.12 | **34.1×** | 🟢 **快 34×** |
| short-1 | 1 / 1 | 1.11 | 1.31 | 1.32 | 1.50 | 1.19× | 🟢 快 1.2× |
| short-3 | 3 / 3 | 1.47 | 1.63 | 1.60 | 1.76 | 1.09× | 🟢 快 1.1× |
| short-2 | 10 / 10 | 4.49 | 5.24 | 2.30 | 2.61 | 0.51× | 🟢 慢 1.95× |
| complex-8 | 20 / 20 | 6.51 | 7.05 | 2.72 | 2.90 | 0.42× | 🟡 慢 2.4× |
| complex-7 | 7 / 7 | 6.92 | 7.51 | 2.64 | 2.90 | 0.38× | 🟡 慢 2.6× |
| complex-11 | 4 / 4 | 9.95 | 10.57 | 3.14 | 3.56 | 0.32× | 🟡 慢 3.2× |
| complex-4 | 0 / 0 | 13.45 | 14.15 | 3.22 | 3.56 | 0.24× | 🟡 慢 4.2× |
| complex-2 | 20 / 20 | 22.05 | 23.06 | 3.78 | 4.06 | 0.17× | 🔴 慢 5.8× |
| complex-10 | 10 / 10 | 67.21 | 71.71 | 7.56 | 8.54 | 0.11× | 🔴 慢 8.9× |
| **complex-12** | 3 / 3 | **459.26** | 476.47 | 91.80 | 100.65 | **0.20×** | 🔴 **慢 5.0×** |
| **complex-3** | 0 / 0 | **527.80** | 538.55 | 24.70 | 26.02 | **0.05×** | 🔴 **慢 21.4×** |

统计：🟢 7 条（其中 4 条快 34–93×）、🟡 4 条、🔴 4 条。

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

### 2.2 用官方加载口径重导数据（消除 §1.1 那些差异的根因）

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

### 2.3 官方 driver 端到端跑通（2026-09-25，进行中）

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

### 2.3.1 官方口径下的实测延迟（1 线程）

> **下表已剔除作废数据**：最初一次运行（warmup 阶段）取自一个**索引不完整、随后崩溃**的实例，
> 其中 short-4/5/6/7 高达 546–1955 ms 是**空索引导致的全表扫描**，不代表引擎水平（详见 2.3.2 第 0/1 条）。
> 那批数字不在此保留。short 系列用 2.3.3 的修正值；复杂查询一栏仍需在"索引完好实例"上重测。

颜色语义同 §2（🟢 相当或更快｜🟡 慢 1.5–5×｜🔴 慢 5× 以上）；neo4j 列为同机、同参数、
同机交替实测（`scripts/bench_ldbc_ab.py`，15 轮取 min）。

| 官方查询 | 对应 | eugraph ms | neo4j ms | vs neo4j |
|---|---|---:|---:|:---:|
| LdbcShortQuery4MessageContent | short-4 | **0.8–1.0** | 76.8–78.4 | 🟢 **快 77–98×** |
| LdbcShortQuery5MessageCreator | short-5 | **0.9** | 162.2 | 🟢 **快 180×** |
| LdbcShortQuery6MessageForum | short-6 | **1.5** | 78.3 | 🟢 **快 52×** |
| LdbcShortQuery7MessageReplies | short-7 | **1.9** | 122.1 | 🟢 **快 64×** |
| LdbcShortQuery2PersonPosts | short-2 | 4.5 | 2.3 | 🟢 慢 1.95× |
| LdbcShortQuery3PersonFriends | short-3 | 1.5 | 1.6 | 🟢 快 1.1× |
| LdbcShortQuery1PersonProfile | short-1 | 1.1 | 1.3 | 🟢 快 1.2× |
| LdbcQuery3 | complex-3 | 527.8 | 24.7 | 🔴 慢 21.4× |
| LdbcQuery12 | complex-12 | 459.3 | 91.8 | 🔴 慢 5.0× |
| LdbcQuery10 | complex-10 | 67.2 | 7.6 | 🔴 慢 8.9× |
| LdbcQuery2 | complex-2 | 22.1 | 3.8 | 🔴 慢 5.8× |
| LdbcQuery4 | complex-4 | 13.5 | 3.2 | 🟡 慢 4.2× |
| LdbcQuery11 | complex-11 | 10.0 | 3.1 | 🟡 慢 3.2× |
| LdbcQuery7 | complex-7 | 6.9 | 2.6 | 🟡 慢 2.6× |
| LdbcQuery8 | complex-8 | 6.5 | 2.7 | 🟡 慢 2.4× |
| LdbcQuery6 | complex-6 | 550–605 | 61–87 | 🔴 慢 8.9×（官方表头实例上直接实测；§2 交错 A/B 未含该条） |
| LdbcQuery5 | complex-5 | 不返回 | 92.3 | 🔴 **挂死**（>400 s，§2.3.2 第 3 条） |
| LdbcQuery1 / 13 / 14 | complex-1 / 13 / 14 | — | — | ⚪ 语法不支持（`shortestPath` 系） |

**复杂查询一栏的口径说明**：`LdbcQuery*` 行取 §2 同机交错 A/B 的 min（参数为本数据集真实
存在的取值）；`LdbcShortQuery*` 行取 §2.3.3 的修正实测。两处都是"同机、同数据、同参数"，
但**样本口径与官方 driver 的随机参数调度不同**，故不与上面那批已作废的 warmup 数字混用。

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

加载完成后**重启实例**（这是 **2026-09-25 旧实现**的现象：索引只写入 catalog、运行中实例内存里没有；
未重启时 `MATCH (t:Tag {name:'Shakira'})` 需 51630 ms，重启后 45 ms）。
**2026-10-20 复测**：当前分支装载后索引即可用、**不需要重启**（见 §9.1）。

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
| A1 | **`CREATE INDEX` 回填阶段 SIGSEGV，服务端崩溃** | 大标签建索引时日志为 `Created vertex index 'idx_msg_cd' (id=12) on Message.(creationDate)` 紧接 `*** Signal 11 (SIGSEGV) ... ***` | catalog 先提交、回填崩溃 → 留下**空索引**；服务端随后无响应 | 2026-10-20 复测未复现（见 §9.1）；根因分析保留在 [known-defects-todo §8](../query/known-defects-todo.md) |
| A2 | **空索引不报错，点查静默退化为全标签扫描** | `Message{id}` 946 ms（索引完好 4 ms）、`Post{id}` 461 ms、`Comment{id}` 495 ms；小标签因回填能完成仍是 1 ms | 症状是"**只在大表上慢**"；所有 `MATCH (... {id: ...})` 类读数可能整体反向 | 2026-10-20 复测未复现；仍需“上线前校验条目数”兜底（与 A1 同源） |
| A3 | **loader 的 DDL 用 30 s 超时且失败只 warn** | `src/program/shell/rpc_client.cpp:47` `channel->setTimeout(30000)`；`csv_loader.cpp:782` 仅 `spdlog::warn` | "加载成功"的实例可能带着一堆空索引，加载流程结束也不校验索引可用性 | 未修 |
| A4 | **complex-5 在官方参数下挂死** | personId=15393162790207 / minDate=1344643200000，单线程 **>400 s 不返回**，多个 compute 线程持续满载；官方 driver 跑到该查询整个 run 停滞 | 官方 benchmark 无法完整跑完 | 未修，见 §5.2 |
| A5 | **规划器对多标签扫描拒绝用索引** | `physical_planner.cpp:2487`：`// Index scan only when single label (multi-label requires runtime intersection)` + `if (scan_op.label_ids.size() == 1)` | `Message`（`Post:Message`/`Comment:Message` 超类）上的属性点查永远不走索引 | 未修（设计取舍，需运行期标签交集校验后才能放开） |

### B1. 数据加载与工具链问题（索引/加载/对照口径）

| # | 问题 | 证据 | 结论 |
|---|---|---|---|
| B1 | **loader 建完索引后不重启实例，索引不生效** | 同一查询在"加载后未重启"为 **51630 ms**，重启后 **45 ms** | 索引只写入 catalog，运行中的实例内存里没有。**加载后必须重启** |
| B2 | **关系类型必须显式指定，否则官方查询全部匹配不到** | `--relationships=person_knows_person=file` 会把类型存成 `person_knows_person`；`MATCH ()-[r:KNOWS]->()` 返回 **0**，而 loader 日志写着 `Loaded 14073 edges for 'person_knows_person'` | **边计数正确、类型名错误**，极其隐蔽；已在 `prepare_ldbc_official_csv.py` 的 `REL_TYPE` 表中显式给出大写关系名 |
| B3 | **原始 CSV 表头缺类型化信息，会丢 schema** | 官方 pipeline 先用 `headers.txt` 覆盖表头再用 sed 改标签值；直接导原始 CSV 时 `Company`/`University`/`City`/`Country`/`Continent` 全部为 0（`type` 只是一列普通属性），complex-11 因此 0 行 | 已提供 `scripts/prepare_ldbc_official_csv.py`（见 §2.2），官方口径下无需任何手工补丁 |
| B4 | **neo4j 侧旧转换的类型/标签差异会让对照失真** | `LIKES.creationDate` 为 `'1342815871582'`（complex-7 直接抛 `TypeError`）；`WORK_AT.workFrom` 为 `'2013'`（与 int 比较**静默不匹配 → 0 行**）；缺 `Message`/`Company`/`University` 派生标签 | 对照前必须先补齐（§1.1）；**"0 行"既可能是引擎错，也可能是对方数据缺**，不核验就会误判 |
| B5 | **两引擎 id 类型不同，参数传错静默 0 行** | eugraph `id` 是 INTEGER（`933`），neo4j 是 STRING（`'933'`） | 跨引擎脚本必须按引擎转型；本项目对照脚本因此有 `id_type` 参数 |

### B2. loader 专项：能力边界 vs 真缺陷

为对齐"我们加载的数据"与"neo4j 加载的数据"，本轮做过的处理及其性质（避免把输入问题误记为 loader 缺陷）：

| 我做过的处理 | 性质 |
|---|---|
| 给 **neo4j** 手工 `SET o:Company` 打派生标签 | 纯 neo4j 侧补丁，与 loader 无关 |
| 用 `prepare_ldbc_official_csv.py` 把表头换成 `headers.txt` 的类型化表头、把 `type` 列值改成 `Company`/`City`… | **输入预处理**——原始 CSV 缺这些信息（官方 pipeline 也做同样的事），见 §1.0 |
| 给 loader 传显式关系类型 `--relationships=KNOWS=file` | **输入映射**——官方 `neo4j-admin import` 同样要求 `--relationships=TYPE=` |

**结论：官方口径下 loader 不缺关键能力**，已核实它支持：

| 能力 | 证据 |
|---|---|
| `:LABEL` 列（一列值 → 多标签） | `csv_loader.cpp:79` `HeaderKind::LABEL`；实测 `Company` 1575 / `University` 6380 / `City` 1343 |
| 类型化表头 | `parseTypedColumn` → `parseTypeName`；实测官方表头加载后 `r.workFrom = 2013`（整数） |
| 多标签节点 | `--nodes=Comment:Message=file`（实测 `Message` 286,744 = Post 135,701 + Comment 151,043） |
| 显式关系类型 | `--relationships=TYPE=file`（声明 `loader_main.cpp:38`，解析为类型 `csv_loader.cpp:178`） |

**真正的 loader 缺陷**（都是"静默通过"型）：

| # | 缺陷 | 证据 | 危害 |
|---|---|---|---|
| L1 | **索引创建失败只 warn、加载结束不校验索引可用性** | `csv_loader.cpp:782` 仅 `spdlog::warn`；与 `rpc_client.cpp:47` 的 30 s 超时共用 | 实例"加载成功"但索引为空 → 点查退化全扫描（946 ms vs 4 ms）。本轮所有错误结论的源头，与 §8 A1/A2 同源 |
| L2 | **关系类型与查询不一致时不告警** | 传 `--relationships=person_knows_person=file` → 类型名即 `person_knows_person`，日志写 `Loaded 14073 edges for 'person_knows_person' (0 skipped)` | 边**计数正确、类型名错误**，`MATCH ()-[r:KNOWS]->()` 返回 0，只有肉眼比对日志才能发现 |
| L3 | **扫描模式的类型名由文件名约定推导，与官方命名不符** | `person_knows_person` → 取首尾下划线之间的 `knows`（不是 `KNOWS`） | 扫描模式吃官方 CSV 时类型名全部不符，必须显式映射 |
| L4 | 不支持从 `type` 列自动派生标签 | 原版 `organisation_0_0.csv` 的 `type=company` 只存成属性 | **设计取舍**（官方也靠 `:LABEL` 列），但意味着 loader 无法直接吃官方原版 CSV，必须先做 §1.0 的表头覆盖 |

> 附：**端点 id 缺失不是静默的**——`csv_loader.cpp:869` 有 `skipped++` 计数并打印
> （本轮所有加载都是 `0 skipped`），这条不算缺陷。

**建议的 loader 改进（按优先级）**：
1. **加载结束自检**：对每个标签做一次"已知存在的 id 点查"，非毫秒级即**报错退出**（并核对 `db.indexes()`），彻底堵住 L1；
2. **DDL 超时独立可配**（`--ddl-timeout`，或建索引单独用长超时）；
3. **关系类型核对**：加载后比对"实际类型 vs `--relationships`/文件名推导"，不一致就 warn；
4. 可选：`--label-from-column=type` 支持从列值派生标签，从而能直接吃官方原版 CSV。

### C. 并发与长时间运行暴露的问题

| # | 问题 | 证据 | 结论 |
|---|---|---|---|
| C1 | **长连接会被服务端断开，并会打挂官方 driver** | 跑到 `LdbcShortQuery2PersonPosts` 时客户端 `ServiceUnavailableException: Connection to the database failed` 并终止整个 run；同一时刻服务端有 `[bolt] read error: ... returned empty buffer` 与 `query cancelled mid-stream` | 即 [known-defects-todo §7](../query/known-defects-todo.md)；自写脚本靠"每查询新建连接"绕过，**官方 driver 不会绕**，是跑完整官方 benchmark 的前置条件 |
| C2 | **并发下点查延迟显著抬高** | 4 并发 short-7：单线程 550 ms → 并发每个 3.2 s（≈6×） | 官方 driver 默认多线程，读到的延迟会高于单线程直测；对比两引擎时必须同线程数 |
| C3 | ~~客户端断开后服务端算子继续算~~ **已复核：取消机制正常** | 严格复测（持续负载 8 s 烧 7.75 s CPU → `kill -9` 客户端）：断开后**只再多算 0.28 s** 即停，随后 45 s 内累计仅 +0.09 s（间断采样） | 取消以**一个迭代为粒度**生效（最坏约 0.44 s），与 [bolt §10](../service/neo4j-bolt-protocol.md) 的设计一致。先前"断开后仍烧 9 分钟"是**误读 `ps` 的累计平均 CPU**所致，已更正 |

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

---

## 9. 2026-10-20 复测（当前分支：连接 / 会话 / 事务 / cursor 重构后）

> 本轮目的：验证 `docs/storage/session-cursor-pool-design.md` 重构后的 Release 构建在 LDBC sf0.1
> 上的装载、查询性能与 neo4j Browser 连通性；同时复核 2026-09-25 的基线是否仍成立。
> **口径仍是同机交错 A/B**（`scripts/bench_ldbc_ab.py`，15 轮取 min），跨会话绝对值只作参考。

### 9.1 装载（官方 CsvBasic 原版语料）

| 项 | 值 |
|---|---|
| 语料 | `social_network-sf0.1-CsvBasic-LongDateFormatter`（官方原版，`params.ini` 同目录） |
| schema | `docs/program/design/ldbc-sf01.schema.json`（**CsvBasic 适配版**） |
| 服务端 | `build-release/eugraph-server`，`--compute-threads 4 --storage-io-threads 4 --thrift-io-threads 4 --wt-cache-size-mb 256 --wt-evict-threads-max 4 --wt-txn-sync fsync` |
| loader | `build-release/eugraph-loader --batch-size 2000 --rpc-connections 4 --parallel-files 4` |
| 装载耗时 | **约 19 s**（`19:55:46` → `19:56:05`） |
| 顶点 / 边 | **327,588 / 1,477,965**，与 8 个顶点 CSV、23 个边 CSV 的**行数逐项相等**（0 skipped） |
| 标签计数 | Person 1,528 / Message 286,744（Post 135,701 + Comment 151,043）/ Tag 16,080 / Forum 13,750 / Organisation 7,955 / Place 1,460（City 1,343 + Country 111 + Continent 6）/ TagClass 71 |

说明：

* 本目录**没有** `loader-schema.json`；同级的 `social_network-sf0.1-CsvComposite-LongDateFormatter/loader-schema.json`
  面向 Composite 表头（含 `language`/`email`、端点列按名字区分），直接用于 CsvBasic 会因缺列与同名端点列报错。
  本轮使用 `docs/program/design/ldbc-sf01.schema.json`（其注释明确声明面向 CsvBasic：忽略
  `person_email_*` / `person_speaks_language`，端点同名列用 `{"index": N}`）。
* loader 只为**主键标签**建唯一索引（8 个）；LDBC short-4/5/6/7 的 `Message{id}` 点查需要额外索引：
  `CREATE INDEX idx_message_id FOR (n:Message) ON (n.id)`。该 DDL 在重构后**正常完成并回填**
  （无 2026-09-25 记录的 SIGSEGV / 空索引问题），建完后 `Message{id}` 点查为 **~1 ms**。
* 索引在本实例上**装载后即可用**，无需重启（§7.1 的“加载后重启”警告是旧实现的状态；本轮已复核）。

### 9.2 Neo4j Browser 连通性

本机 Neo4j Browser（`http://127.0.0.1:7474/browser/`）连接 eugraph：

* 连接地址 `bolt://localhost:7688`，用户名 `neo4j`，密码 `eugraph`（eugraph 的 BASIC 口令约定）；
* Browser 页面返回 200，Bolt **WebSocket upgrade 返回 101**；
* 实测 Browser 连接成功：`connection status: connected` / `Connected to bolt://localhost:7688`。

复测中发现并修复了一个阻塞 Browser 登录的缺陷：Browser 的元数据查询（`SHOW CURRENT USER`,
`SHOW DATABASES`, `CALL dbms.components()`…）使用**显式事务**并指定 `db=system`，而 `system`/`neo4j`
到 default 图的别名此前只写在 `GraphService::executeCypher` 里；显式事务走
`GraphService::beginSession → resolveGraph("system")`，未做别名 ⇒ `Graph not found: system`。
修复：别名收敛到 `GraphService::resolveGraph`（所有入口共用的单点）。回归：`rpc_integration_tests` 26/26。

### 9.3 同机交错 A/B（单位 ms）

| 查询 | 行数 eg/neo | eg min | eg p50 | neo min | neo p50 | vs neo4j | 旧 eg min | 相对旧版 |
|---|---:|---:|---:|---:|---:|:---:|---:|---:|
| short-1 | 1/1 | 1.00 | 1.35 | 1.52 | 2.10 | 1.51× | 1.11 | -9.5% |
| short-2 | 10/10 | 4.07 | 4.41 | 2.69 | 3.30 | 0.66× | 4.49 | -9.4% |
| short-3 | 3/3 | 1.13 | 1.37 | 1.52 | 2.12 | 1.34× | 1.47 | -22.8% |
| short-4 | 1/1 | 1.17 | 1.28 | 84.63 | 89.12 | 72.19× | 1.35 | -13.2% |
| short-5 | 1/1 | 1.21 | 1.33 | 161.01 | 167.02 | 132.67× | 1.53 | -20.7% |
| short-6 | 1/1 | 1.56 | 1.82 | 85.05 | 87.44 | 54.37× | 2.01 | -22.2% |
| short-7 | 0/0 | 1.74 | 1.96 | 120.15 | 123.17 | 68.95× | 2.34 | -25.5% |
| complex-2 | 20/20 | 18.54 | 20.07 | 4.06 | 4.75 | 0.22× | 22.05 | -15.9% |
| complex-3 | 0/0 | 474.91 | 486.30 | 27.99 | 29.14 | 0.06× | 527.80 | -10.0% |
| complex-4 | 0/0 | 12.81 | 14.26 | 3.43 | 3.88 | 0.27× | 13.45 | -4.7% |
| complex-7 | 7/7 | 6.77 | 7.13 | 2.99 | 3.29 | 0.44× | 6.92 | -2.2% |
| complex-8 | 20/20 | 5.55 | 6.24 | 2.78 | 3.41 | 0.50× | 6.51 | -14.8% |
| complex-10 | 10/10 | 62.76 | 66.31 | 7.24 | 8.66 | 0.12× | 67.21 | -6.6% |
| complex-11 | 4/4 | 8.72 | 9.32 | 3.64 | 4.25 | 0.42× | 9.95 | -12.3% |
| complex-12 | 3/3 | 422.23 | 439.14 | 118.47 | 125.36 | 0.28× | 459.26 | -8.1% |

`vs neo4j = neo_min / eg_min`（>1 表示 eugraph 更快）；`相对旧版 = (新 eg_min - 旧 eg_min)/旧 eg_min`。

### 9.4 并发 QPS（对照 aa7cd5ca 的并发优化）

工作负载：`interactive-complex-9.cypher`（`personId=933`、`maxDate=1289908800000`、database `default`）。
N 个 client = N 条长连接（每 client 一个 driver）；每 client 先 warmup 2 次，barrier 对齐后连续执行
`--duration` 秒；QPS = 完成批次数 / wall。脚本：`scripts/bench_bolt_qps.py`（本轮新增，保持长连接，
与每查询新建连接的 `bench_ldbc_ab.py` 刻意区分）。

| clients | compute_threads | QPS | p50 ms | p95 ms | 旧基线（aa7cd5ca，complex-9） |
|---:|---:|---:|---:|---:|---:|
| 1 | 4 | 2.15 | 464 | 480 | — |
| 4 | 4 | **6.89** | 570 | 604 | 6.10（4 threads） |
| 8 | 4 | 6.58 | 1174 | 1423 | 5.51（8 线程、compute=4 的引擎 QPS） |
| 4 | 8 | 6.63 | 597 | 646 | — |
| 8 | 8 | **9.87** | 796 | 869 | 8.41（8 threads） |
| 8 | 8 | **9.99**（30 s 稳定性复跑） | 792 | 858 | — |

结论：

* **4 client / compute=4**：6.89 vs 旧 6.10（**+13%**）；**8 client / compute=8**：9.87–9.99 vs 旧 8.41
  （**+17%~+19%**），也高于 aa7cd5ca 记录的通用 9.36。相对优化前的 2.51 / 5.51 仍是 **2.7× / 1.8×**。
* **compute_threads ≥ clients** 的旧结论仍成立：8 client 在 compute=4 时饱和在 6.58（p95 1.42 s），
  切到 compute=8 后升到 9.87（p95 0.87 s）。4 client 时 compute=4 与 8 基本同档（6.89 / 6.63，噪声内）。
* **稳定性**：30 s × 8 client 共 305 次 complex-9，无错误、无连接断开（20 s 与 30 s 两次结果 9.87/9.99）。
  本轮没有跑官方 driver 的长连接矩阵（见 §8 C1），长跑结论仍以那条为准。
* **存储层微基准**（`tests/tools/concurrency_bench.cpp`：每线程独立事务，`Comment.creationDate`，
  每线程 20,000 次查找，3 轮取 min）：8 线程 **15.80 M/s（小工作集）/ 14.23 M/s（大工作集）**，
  旧记录为 4.88 M/s；1→2→4→8 线程为 3.09→5.96→10.32→15.80 M/s（小工作集），仍随线程数上升。
  **口径提醒**：新实现的 point get 复用会话级 cursor（旧版每次 open/close），路径更短，
  因此这个数字只能作方向性验证（没有并发退化），不能与 4.88 M/s 当作严格 A/B。
  该工具在本轮之前已无法编译（`SessionPool::acquire(node)` 与 `store.close()` 均已删除），
  本轮一并修复为当前 API。

### 9.5 结论

* **行数两引擎全部一致**（包括 complex-3 / complex-4 的 0 行）——A/B 可用的前提成立。
* **绝对值**：15 条全部不高于旧文档（−2.2% ~ −25.5%）。跨会话绝对值受 CPU 频率影响，
  只作方向参考；本轮同机 neo4j 的读数也比旧文档慢 10–30%，进一步说明不能拿“旧 neo4j / 新 eugraph”直接相减。
* **相对差距全面收窄**：complex-3 21.4×→17.0×、complex-12 5.0×→3.6×、complex-10 8.9×→8.3×、
  complex-2 5.8×→4.6×、complex-11 3.2×→2.4×、complex-8 2.4×→2.0×；short-4/5/6/7 分别快
  72×/133×/54×/69×（旧文档 52×/93×/34×/48×）。short-1/3 仍略快于 neo4j，short-2 仍慢约 1.5×。
* **剩余大头**仍是 complex-3（~17×）与 complex-12（~3.6×），形态与旧文档 §2.1 一致：
  具名起点多跳 + 中间结果放大；没有因本轮重构而恶化。
* **并发无回退、且优于旧基线**：complex-9 4/8 client 为 6.89 / 9.87–9.99 ops/s（旧 6.10 / 8.41），
  详见 §9.4；存储层 8 线程 15.80 / 14.23 M/s（方向性）。
* **数据计数差异**：neo4j 实例为 330,130 点 / 1,478,003 边，比原始语料多 2,542 点 / 38 边；
  多出部分来自历史测试标签（`CT`、`CPerson`、`UT_*`、`M1*` 等），LDBC 相关标签计数与查询行数均一致，
  不影响本轮对照。旧文档 eugraph 的 330,126 / 1,478,002 未在本轮复现；本轮以原始语料行数为准。

**复现命令**：见 §7.3（A/B）、本轮 §9.1（loader）与 §9.4（`scripts/bench_bolt_qps.py`）；
A/B 可用 `--json` 保存原始结果（本轮结果未入库）。

---

## 10. complex-3 专项优化（2026-10-21）

> 目标：complex-3 是上一轮差距最大的查询（~17–21×）。本轮用同机交错 A/B + GDB 采样定位，
> 修掉了三处**逐行重复物化/空查询**，不改查询语义、不改存储格式。

### 10.1 根因（按贡献排序）

1. **`ProjectionExtract` 为每个输出行深拷贝 `VertexValue`**：执行
   `WITH DISTINCT friend, countryX, countryY` 后，friend/countryX/countryY 都是完整 VERTEX；
   随后的 `Expand(friend→message)` 把 52,475 条 message 展开，`ProjectionExtract` 对每一行
   重新构造这 3 个顶点，深拷贝 `unordered_map<LabelId, Properties>`。
   GDB 采样里 `VertexValue` 的构造/析构是最显眼的一类栈。
2. **批处理 API 对重复 vid 逐个处理**：同一批 1024 行里的 friend/countryX/countryY 往往是
   **同一个 vid 重复 1024 次**；`getVertexLabelsBatch` / `getVertexPropertiesBatch` /
   `...Projected` 仍对每个元素做一次 WT `search_near`。
3. **无条件 anon 属性查询**：`ProjectionExtract` 对每个 ConstructVertex 行都查一次
   `__anon__` 属性，即使该顶点根本没有 anon 标签。
4. **为无属性边逐行查 eprop 表**：`ConstructEdge` 对每行都 `getEdgeProperties`，而
   `HAS_CREATOR` / `IS_LOCATED_IN` 根本没有声明属性；52k 条 message 产生 52k 次空 WT 查询。

### 10.2 改动

| 文件 | 改动 |
|---|---|
| `src/storage/data/sync_graph_data_store.cpp` | 三个 batch API 先按唯一 vid 扫描、再按原顺序展开；重复 vid 只做一次 WT 查询 |
| `src/query/physical_plan/operator/projection_extract_physical_op.cpp` | ConstructVertex 缓存改为 `VertexValuePtr`：同一 vid 在批内**共享**一个实例；ref 源按唯一 vid 物化一次；只在需要清 `INVALID_LABEL_ID` / 补 anon 时写时复制 |
| 同上 | anon 属性只对 labels 真的包含 `__anon__` 的顶点查询 |
| 同上 + `physical_planner.cpp` | `ConstructEdge` 按 schema 的边属性数跳过空 eprop 查询（无属性边不再逐行查表） |

### 10.3 同机交错 A/B（同一数据、同一客户端；baseline = 优化前 main）

工作负载：`interactive-complex-3.cypher`（personId=933），每轮 3 warmup + 10 次计时，三轮交替；
另有中间量 `MATCH (friend)<-[:HAS_CREATOR]-(message) RETURN count(message)`（52,475 行）用于隔离
message 展开。

| 指标 | baseline min / p50 | 优化后 min / p50 | 提升 |
|---|---:|---:|---:|
| complex-3 | 489.8 / 497.0 ms | **136.0 / 141.9 ms** | **3.60× / 3.50×** |
| message 展开子查询 | 199.4 / 206.0 ms | **31.1 / 32.7 ms** | **6.4× / 6.3×** |

complex-3 行数两端都是 0（与 §9 一致）。neo4j 同查询约 **27–44 ms**（取决于机器负载），
即优化后 eugraph 仍慢约 **3–5×**，已从 ~17–21× 收窄到同一数量级。

### 10.4 与 neo4j 计划对照（关键：它**也**读了 52k 次 creationDate）

neo4j `PROFILE`（5.26）的算子链：

```
Expand(All) (friend)<-[:HAS_CREATOR]-(message)      52,475 rows
  Filter (cache[message.creationDate] < end AND >= start)   DB Hits 52,475 → 787 rows
    Expand(All) (message)-[:IS_LOCATED_IN]->(country)       787 rows / DB Hits 5,319
      Filter country IN [countryX, countryY]                18 rows
        Projection CASE ... → EagerAggregation → ... → CacheProperties → ProduceResults
```

要点：

* **neo4j 同样对 52,475 条 message 读了 creationDate**（Filter 的 DB Hits 就是 52,475），
  **没有**用 creationDate 索引，也**没有**把 date 谓词下推到 HAS_CREATOR 之前；
  join 顺序与 eugraph 相同（都是 friend → message → date → country）。
* 差别在**每次属性读取的代价**与**何时物化整顶点**：

| 维度 | neo4j | eugraph（优化后） | 差距来源 |
|---|---|---|---|
| creationDate 读取 | 节点记录内属性指针，52,475 次 DB hits ≈ **+15.6 ms**（messages 11.5→27.1） | WT `vprop` KV：每 message 先读 labels、再按候选 prop 读值，52,475 次 ≈ **+86 ms**（messages 41→127） | 属性/标签**存储布局**：逐属性 KV + 独立 label_reverse 表 vs 节点内联 |
| 52k HAS_CREATOR 展开 | 邻接链表，11.5 ms | WT edge index，31–41 ms | 每源一次 B-tree `search_near` vs 指针链表 |
| 整顶点物化 | 只保留引用；`CacheProperties` 仅对最终 20 行 | 优化前每行深拷贝 friend/country；现在批内共享，但仍按行经过 ProjectionExtract | 本轮已消除深拷贝，剩余为逐行 slot/句柄成本 |
| 无属性边 | EdgeValue 直接是引用/空属性 | 优化前逐行查 eprop（本轮修复） | 本轮已消除 |

* 因此本轮先修的是**低级重复**（深拷贝、重复 vid、空 eprop 查询）；进一步缩小差距需要动
  **存储布局/读取原语**（见下节），而不是继续调算子。

### 10.5 剩余瓶颈与下一步

* **~80–90 ms：creationDate 的 labels + property 两次 WT 查询**。当前 `LoadVertexPropCoalesce`
  先 `getVertexLabelsBatch(52k)` 判断 message 是 Post 还是 Comment，再按候选 `(label,prop)`
  读 52k 次属性。neo4j 把 labels 内联在节点记录、属性内联/指针化，天然只需一次读。
  可选方向（按侵入性）：
  1. **把 labels 与常用属性放在同一条 vertex KV**（`vertex_record{labels, prop_map}`），
     读一次即可同时得到 label 与 creationDate；这是存储格式变更，需要独立设计与兼容性评审。
  2. 若查询已能静态确定 label（强类型/单标签），只读一个 `(label,prop)`，跳过 label 查询；
     对 `Message` 这种派生多标签不适用。
  3. 为高选择性谓词建二级索引（如 `Message.creationDate`），由 CBO 选择“索引范围扫描 + join”；
     但 LDBC 官方 neo4j 也未走索引，不能作为通用替代。
* **~30–40 ms：52k HAS_CREATOR 邻接展开**。WT edge index 每源一次 `search_near`；
  `scanEdgesBatch` 复用一把 cursor 只能省 open/close（本轮实测 <5%，未保留）。
  要再降需要**按源连续的邻接存储**（per-vertex adjacency row/table）或覆盖索引，同属存储布局变更。
* 本轮未改变结果语义；`build-release` 全量 ctest **1294/1294** 通过（含 tck）。
