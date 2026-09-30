# Loader 设计

> [当前实现] 参见 [docs/README.md](../../README.md) 返回文档导航
> 使用文档见 [loader.md](../usage/loader.md)

## 1. 动机

### 1.1 现状

当前 loader 的装载流程建立在一个隐含前提上：**顶点身份 = 服务端分配的 `VertexId`**。由此产生三处耦合：

1. **Loader 维护全量 ID 映射**（[csv_loader.hpp](../../../src/program/loader/csv_loader.hpp#L61-L66)）：

   ```cpp
   using CsvIdMap = std::unordered_map<std::string, std::unordered_map<int64_t, uint64_t>>;
   struct LoadedIdMaps {
       CsvIdMap group_id_map;                                       // group -> (csv_id -> vertex_id)
       std::unordered_map<std::string, std::string> label_to_group; // label -> group
   };
   ```

   映射大小与**顶点数**成正比。本仓语料（sf0.1）实测 **327,588 个顶点 / 1,477,965 条边**，映射约 33 万条（数十 MB 量级，sf0.1 下尚可接受）；SF1 约 320 万、SF10 约 3200 万顶点，按 `unordered_map` 每条约 48–64 字节计即 GB 量级。更关键的是**全部映射必须在开始装载边之前驻留完毕**，内存随规模线性增长且无处释放。

3. **Thrift 写入协议携带 `VertexId`**（[proto/eugraph.thrift](../../../proto/eugraph.thrift#L99-L108)）：边记录携带服务端内部 ID，意味着外部程序必须知道内部 ID，内部 ID 事实上成了对外协议的一部分；会话/增量装载场景下完全不可用。

4. **CSV 表头没有类型与主键声明**。`social_network-sf0.1-CsvComposite-LongDateFormatter` 的表头只有属性名：

   ```text
   id|creationDate|locationIP|browserUsed|content|length        (comment_0_0.csv)
   Person.id|Comment.id|creationDate                            (person_likes_comment_0_0.csv)
   ```

   没有类型后缀、没有 `:ID` / `:START_ID` / `:END_ID`。loader 目前靠三条兜底路径：
   - 无类型后缀的列：采样前 100 行推断 INT64/STRING（[csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L299-L318)）——**类型靠猜**；
   - 主键列：默认第 0 列且**硬编码 INT64**（[csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L92-L98)）——**类型不可配置**；
   - 边端点分组：靠 `Comment.id` / `Person.id` 这种「表名.列名」形状或文件名猜 group（[csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L812-L830)）——**无声明、纯约定**。

   neo4j 的做法是**改写 CSV 表头**（`cypher/scripts/headers.txt` + `convert-csvs.sh`）并 `sed` 把 `:LABEL` 小写值改成首字母大写，代价是必须**派生一份改写过表头的 CSV 副本**。本方案改为**声明外置**，不动原始 CSV。

### 1.2 目标

| 编号 | 目标 | 手段 |
|---|---|---|
| G1 | Loader 不再维护「主键 → VertexId」映射 | 边记录携带**主键引用**，服务端解析 |
| G2 | Thrift 写点/写边协议不携带 `VertexId` | `VertexRecord.pk` / `EdgeRecord.src,dst` 改为 `(label, pk_name, pk_value)` |
| G3 | 属性类型与主键外部声明，不改写 CSV | **JSON schema 配置（唯一声明来源）**；含时序类型 |

### 1.3 非目标

- 不改 Cypher `CREATE` / `MERGE` 语义（`CREATE` 仍分配新 VID，不查主键）；
- ~~主键索引接入查询规划器~~ —— **已纳入本次范围**（主键就是普通索引，`tryBoundIndexScan` 直接可用，无需额外工作）；
- 规划器**跨标签**复用索引（如用 `(n:place)` 的索引服务 `MATCH (n:city {id:5})`）不在本次范围；
- 不改 KV 编码（不新增表，完全复用 `vidx_*`）；
- 不做「点边交错流式装载」与「增量补边」（主键化使其可行，但作为后续独立任务）；
- 不做主键以外的**跨表/跨文件主键一致性校验**（例如两个文件都往同一标签写、却声明了不同的主键）。

---

## 2. 已确认的设计口径

| # | 决定 | 口径（本轮确认） |
|---|---|---|
| 1 | 主键**可选**，由配置声明 | 没有主键**不影响导入**；主键的用途是「装载时写入索引、装载边时用索引解析端点」，并顺带让查询点查可走索引 |
| 2 | 主键 = 一个普通 `UNIQUE` 索引，**不新增任何存储** | 由现有索引管理器承载（`LabelDef.indexes` → `vidx_{index_id}`）；未声明主键的标签零额外开销 |
| 3 | 主键冲突 | **first-wins**：保留先写入者，仅告警（不阻断装载） |
| 4 | 声明主键 → **必须创建该列的唯一索引**（它就是主键） | 建在 **schema 键（主标签）** 上 + 弱 accessor；一条索引覆盖该标签全部顶点（含行级标签产生的额外标签）（§4 决定 1、§6.3） |
| 5 | 类型/主键声明载体 | **JSON 配置文件**（`--schema`）——**没有命令行覆盖**：主键/类型/分隔符/时间格式全在 JSON 里，保证「同一份 schema 必然装出同一张图」 |
| 6 | 行级标签 | **指定即所用**：配置里写 `city` 就是 `city`，写 `City` 就是 `City`，不做隐式转换；它只是**追加**标签，不再抢占主标签（§6.4） |
| 7 | 时序类型 | **本次一起做**：按 `date_format` 解析 epoch 或 ISO-8601 |
| 8 | 存储兼容性 | **不承担**：元数据编码直接改，不写旧格式解码分支、不做迁移测试 |
| 10 | 装载入口 | **`--schema` + `--data-dir` 必填**；`--nodes` / `--relationships` 与目录扫描模式**删除**（§4 决定 3） |
| 9 | 两个默认值 | 时序 `date_format` 默认 `epoch_ms`（本仓语料是 Long 毫秒；ISO 数据显式写 `"iso"`）；`schema-strict` **给了 `--schema` 就默认开启、没给则关闭**（理由见 §4 决定 3） |

---

## 3. 输入侧的硬约束：不改用户文件，就必须能读他们的文件

**设计前提（本节的验收口径）**：用户的 CSV/Excel 导出**原样不动**，loader 必须自己适配。语料实测（31 个文件）没有暴露这些问题 —— 无 BOM、无 CRLF、无引号、每行列数都等于表头 —— 但**用户自己的文件会有**。以下四条是本次必须补的读取能力，否则"不改用户文件"这条前提就不成立。

| # | 风险（用户文件常见形态） | 现状行为（实测） | 本次要求 |
|---|---|---|---|
| R1 | **UTF-8 BOM**：Excel「另存为 CSV」默认带 BOM | 表头首列变成 `"\uFEFFid"`，与 schema 里的 `id` 不匹配 → strict 报「列未声明」；非 strict 下变成一个**幽灵属性** `\uFEFFid` | 读表头时剥离 BOM（仅首列、仅表头行）；同时在启动时检查文件内是否存在其它 BOM |
| R2 | **带引号的字段**（RFC 4180 合法）：`"a&#124;b"`、`"say ""hi"""` | 引号被当普通字符；**引号内的分隔符会把字段错误切开**：`"a&#124;b"&#124;x` → 切成 3 个字段，导致列错位（静默数据损坏） | 实现规范引号解析：引号内的分隔符/换行不切分，`""` 解转义为 `"` |
| R3 | **非 `&#124;` 分隔符**：Excel 默认逗号、TSV 制表符，个别语料用多字符分隔符 | 旧 loader 的 `--delimiter` 只接受 `&#124;`，其它直接报错退出 | 分隔符写在 schema（顶层默认 + 文件级覆盖），支持 `,` / `;` / `\t` / `&#124;` **及任意多字符**；不引入第三方 CSV 库 |
| R4 | **字段内含未转义分隔符**（脏数据） | 该行列数多于表头 → 静默错位 | 每行做**列数校验**：不等于表头列数即报错，消息含 `文件:行号:期望列数:实际列数`，并提示"如字段本身含分隔符请加引号，或在 schema 里改 `delimiter`" |

**不做的（明确边界）**：不支持无表头文件（loader 的前置假设就是"都有标头"）；不做编码嗅探（非 UTF-8 需用户自行转码，报错时会提示）。
~~不处理多字符分隔符~~ —— **已支持**：分隔符进 schema 后摆脱了 CLI 的单字符限制，`resolveDelimiter` 接受任意非空、非纯空白、不含引号/换行的字符串，读取时按**整体**匹配。

**错误消息的可操作性**（与 §5.3 的解析错误同一要求）：

| 场景 | 现有可能的抱怨 | 要求的报错 |
|---|---|---|
| schema 里声明的文件不存在 / `file` 拼错 | 静默无输出 | 启动时**校验 `--schema` 里每个 `file` 都存在**（相对 `--data-dir`），缺失即报错列出 |
| 数据目录里有文件没在 schema 声明 | strict 下报"列未声明"，看不出是文件级问题 | 报错时**同时指出文件未在 schema 中声明**（区分"文件漏声明"与"列漏声明"） |
| 行内列数不符 | 无提示，错位 | `文件:行号:表头列出 N 列，本行 M 列` |

---

## 4. 核心设计

### 决定 1：主键就是一个普通索引，不特殊建表

**口径**：**主键索引与其他索引没有区别** —— 它就是一个普通的 `UNIQUE` 索引，由现有索引管理器（`LabelDef::IndexDef` → `vidx_{index_id}` → 回填 / 写路径维护 / `IndexScan` 优化）统一承载；loader 只是在装载时**优先用主键索引**做端点解析。

**问题一**：还要不要单独建 `table:pk_{label_id}`？**不要**。索引管理器已经提供了我们需要的全部能力：

| 需求 | 原方案的独立 PK 表 | 主键即唯一索引（采用） |
|---|---|---|
| 端点解析点查 | 自己实现 | `scanVerticesByIndexId(index_id, {pk_value})`（[i_async_graph_data_store.hpp](../../../src/storage/data/i_async_graph_data_store.hpp#L172)） |
| 唯一性检查 | 自己做 | `checkUniqueConstraint` + `insertVertexIndexEntriesChecked`（[vertex_index_maintenance.hpp](../../../src/query/physical_plan/operator/vertex_index_maintenance.hpp#L207-L220)） |
| 查询加速 `MATCH (n:Person {id: $x})` | 接入规划器 = 另一个任务 | **免费**：`tryBoundIndexScan` 只认 `LabelDef.indexes`（[physical_planner.cpp](../../../src/query/physical_plan/physical_planner.cpp#L1023-L1053)） |
| 删除 / 改属性时的维护 | 要自己接 | 现有 `collectVertexIndexEntries` / `deleteVertexIndexEntries` 已覆盖全部标签与弱 accessor |

**问题二**：既然主键就是索引，还需要 `LabelDef.pk_prop_ids` 吗？**保留，但只作为「哪个属性是主键」的声明**（不再是存储结构）：

- `batchInsertVertices` 必须**立刻**知道对哪个属性做唯一性预检，`batchInsertEdges` 必须知道解析哪个属性。仅从 `indexes` 反推需要「恰好一个 UNIQUE 索引、且它就是主键」这类约定，而用户完全可能再对别的列建 UNIQUE 索引 —— 届时无法区分（这是必须显式声明的主因）；
- 代价是每标签 `2 字节 × 主键列数`，而 `LabelDef` 本来就要持久化索引定义；
- **两者是同一份事实的两面**：`pk_prop_ids` 说「谁是主键」，`indexes` 说「索引长什么样」；`createLabel` 同时收到并校验一致性（§6.1）。

**问题三**：first-wins 怎么和唯一约束共存？**预检 + 跳过，让冲突根本不发生**：

```text
写入一条带主键的顶点时：
  1. 若该标签已有 live 主键索引：checkUniqueConstraint(该索引, 主键值)
     - 已存在 → first-wins：跳过该记录、计数 +1、WARN（不写顶点、不写索引）
  2. 通过 → insertVertex(...) + 写索引条目
```

于是重复主键值**永远不会进入索引** → `CREATE UNIQUE INDEX` 的回填不会撞冲突 → 索引正常落到 `PUBLIC`。前两轮设计里 E1/E2 说的「唯一索引与 first-wins 冲突」由此消解：**不是让唯一索引容忍重复，而是在写入前就不产生重复**。

**问题四**：索引建在哪个标签上？**建在点文件的文件级标签上，用弱 accessor**：

```text
CREATE UNIQUE INDEX idx_place_id_unique FOR (n:place) ON (n.id)
```

**一条索引覆盖该标签的全部顶点**，因为：

- 主标签就是 **schema 的键**（如 `place`），每个顶点都带它；行级 `label.header` 只是**追加**标签（如 `city`），于是 `place` 标签下包含该文件的全部 1460 个顶点；
- 弱 accessor 按属性名在实体的所有标签里取值（[vertex_index_maintenance.hpp](../../../src/query/physical_plan/operator/vertex_index_maintenance.hpp#L70-L80)），所以行级标签不影响取值；
- 回填扫描 filter label（`place`）下的实体，全部顶点进同一张 `vidx_{index_id}`。

不需要为每个行级标签各建一条唯一索引：索引数量从 N 降到 1，回填与写入维护只付一份，也不存在「同值条目散落在多条唯一索引、每条各自查重」的冗余。

**解析规则**（服务端 `batchInsertEdges` 内）：

```text
resolveEndpoint(label_hint, pk_name, values):
  1. label_hint 有效 → 取该标签的主键索引 index_id → scanVerticesByIndexId(index_id, values)
     - 1 条命中 → 返回
     - ≥2 条命中 → 取 vid 最小者 + WARN（只可能来自并发双写；first-wins 下不报错）
  2. 未命中 → 遍历 GraphSchema 中「主键名 == pk_name」的其它标签，逐个按 1 查
     - 恰好 1 个命中 → 返回（+ WARN 记录「hint 标签未命中、按同名主键回退」）
     - ≥2 个标签命中 → 取 vid 最小者 + WARN（跨标签同名主键歧义）
     - 0 个 → 端点不存在 → skipped_unresolved
```

第 2 步的遍历范围是所有声明了同名主键的标签（≤ 图内标签数，通常个位数），只在第 1 步未命中时才发生 —— 属于兜底路径：正常情况下 `src_label`/`dst_label` 必填且经启动校验，第 1 步就能命中正确的索引。

**元数据**（[graph_types.hpp](../../../src/common/types/graph_types.hpp#L176-L192)）：

```cpp
struct LabelDef {
    LabelId id;
    LabelName name;
    std::vector<PropertyDef> properties;
    std::vector<uint16_t> pk_prop_ids;   // 新增：主键属性 ID（空 = 无主键）
    std::vector<IndexDef> indexes;
};
```

- 直接存 `prop_id`（不做「存名字、运行时按名解析」的间接层）：索引条目本身按 `prop_id` 组织，主键声明与索引 accessor 必须指向同一属性，DDL 阶段一次性解析成 ID 最直接；`pk_prop_ids` 与 `indexes` 因此是同一份事实的两面（同一次 DDL 写入）；
- **顺序即语义**：`pk_prop_ids` 的顺序 = 主键元组的顺序 = 索引 accessor 的顺序，三者必须一致（复合主键见 §5.4）；
- DDL 层（`createLabel`）以**主键属性名**为对外参数，由 `AsyncGraphMetaStore` 写入 `LabelDef` 前解析成 `prop_id`（属性名 → prop_id 是标签内的一次线性查找）。

### 决定 2：Thrift 写协议携带主键，不携带 VertexId

```thrift
struct PkKey {
  1: string name                       // 主键属性名，如 "id"
  2: PropertyValueThrift value
}

// 边端点：目标顶点的（主标签, 主键名, 类型化主键值）
struct PkRef {
  1: string primary_label              // 目标顶点的主标签；必填（服务端据此选索引）
  2: list<PkKey> keys                  // 主键键值，**顺序与主键声明一致**（复合主键即多个）
}

struct VertexRecord {
  1: list<PropertyValueThrift> properties
  2: list<string> labels               // 完整标签列表，第一个是主标签
  3: list<PkKey> pk                    // 新增：该顶点的主键；空 = 该顶点无主键
}

struct EdgeRecord {
  1: PkRef src                         // 取代原 src_vertex_id
  2: PkRef dst                         // 取代原 dst_vertex_id
  3: list<PropertyValueThrift> properties
}

struct BatchInsertVerticesResult {
  1: list<i64> vertex_ids
  2: i32 count
  3: i32 inserted
  4: i32 duplicate_pk                  // 因主键已存在/批内重复而未写入的记录数（first-wins，不报错）
}

struct BatchInsertEdgesResult {        // 新增（取代 i32 返回）
  1: i32 inserted
  2: i32 skipped_unresolved            // 端点解析不到（对应现状 loader 侧的 skipped 计数）
}

service EuGraphService {
  BatchInsertVerticesResult batchInsertVertices(1: string label_name, 2: list<VertexRecord> records, 3: string graph_name)
  BatchInsertEdgesResult    batchInsertEdges(1: string edge_label_name, 2: list<EdgeRecord> records, 3: string graph_name)
}
```

要点：

- **字段直接删、编号直接重排**：本次不考虑 RPC 兼容性。`src_vertex_id` / `dst_vertex_id` 整字段删除，`src` / `dst` 占用 1、2，`properties` 仍是 3（位置天然不变）。这与本仓既有惯例一致：commit `f523c17`（remove pk index）删掉 `VertexRecord.pk_value` 后，把 `properties` 从 2 号重编为 1 号。**不保留 reserved 编号，不做双写兼容**；
- **`PkRef.primary_label` 是「目标顶点的主标签」，不是「分组名」**：服务端靠它选对索引（`table:vidx_{index_id}`）。loader 侧来源见 §7.3：边文件表头的 `:START_ID(Label)` / `:END_ID(Label)` → `Label.id` 形态的列名 → 文件名约定；
- **`primary_label` 是索引选择提示**：loader 给的可能是命名空间标签（`Place`）而顶点主标签是 `city`，服务端第 1 步选索引未命中时会按「同名主键的其它标签」回退（§4 决定 1 的解析规则第 2 步）；
- **`VertexRecord.labels[0]` 才是权威主标签**：服务端按它决定属性落在哪个标签、主键条目写进哪张索引；`PkRef.primary_label` 只是冗余的索引选择信息（两端都来自同一份配置/表头，不会互相矛盾）；
- **`PropertyValueThrift` 是 union**：主键值必须是**已定类型**的值（loader 按声明的类型解析后放入），不允许「字符串裸值让服务端猜类型」；
- **`pk` 可为空**：与口径 1 一致 —— 没声明主键的点文件照常导入，只是它的顶点不能被边引用；
- 生成代码按 [build-guide.md](../../build/build-guide.md) 重新生成 `src/service/thrift/gen-cpp2/`，禁止手工修补。

### 决定 3：JSON schema 配置

**唯一输入就是 `--schema`（本轮决定：移除 `--nodes` / `--relationships`）**

原来的 loader 有**两条装载模式**：`--nodes`/`--relationships` 显式映射（CLI 模式）与目录扫描（按文件名约定猜标签/边类型）。现在 schema 配置文件已经能完整表达「哪个文件、什么标签、哪些列、什么类型、主键是谁、端点是谁」，CLI 映射参数就是**同一份信息的第二种写法**——两套并存只会带来两套代码路径、两套报错和两套文档。因此：

| 项 | 处置 |
|---|---|
| `--nodes` / `--relationships` | **删除**（不再有 CLI 映射模式） |
| 目录扫描模式（按文件名猜标签） | **删除**（文件由 schema 指定；`scanCsvFiles` 及其分类逻辑一并移除） |
| `--data-dir` | **变为必填**：schema 里的 `file` 相对它解析，装载哪些文件也由 schema 决定 |
| `--schema` | **变为必填**（唯一入口） |
| 表头作为声明来源（`:ID` / `:START_ID(Group)` / `name:TYPE`） | **不再读取**（表头只用于**按名定位列**） |
| 采样推断 / STRING 兜底 | **仅作用于未在 `columns` 声明的列**，且受 `schema-strict` 控制 |

于是优先级链从一个五层叠加体系塌缩成「一份声明 + 一个宽松开关」：

```text
① --schema <file.json>   唯一的完整声明（主键 / 类型 / 分隔符 / 时间格式 / 行级标签）
② 未声明的列：strict 报错（列出文件与列名）；`--no-schema-strict` 时回落到采样推断并打 INFO
```

命令行不再提供任何声明覆盖（旧的 `--pk` / `--types` / `--date-format` / `--delimiter` 已删除）：
它们会制造**第二份真相**——schema 是入库、可 review 的，命令行历史不是；同一个 `--pk` 改一下
就能装出另一张图（主键换列 → 唯一索引变 → 边端点连到别的顶点），而仓库里没有任何记录。

**`schema-strict` 默认值（已定）**：默认**开启**（`--schema` 已是必填，配置的意义就是显式声明，漏列应当报错指出）；`--no-schema-strict` 可显式关闭，此时未声明的列才回落到采样推断。

> `--no-schema-strict` 是**运行方式**（同一次运行要不要宽容），不是数据声明，因此它是唯一保留在命令行上的宽松开关。

**站点收益**：loader 少掉约 158 行（`scanCsvFiles` 的文件分类 + `parseNodeSpec`/`parseRelationshipSpec`），`CsvFileInfo` 的 `is_vertex` / `src_label` / `edge_type` / `dst_label` / `labels` 五个 legacy 字段与 4 个函数声明一并消失（[csv_loader.hpp](../../../src/program/loader/csv_loader.hpp#L25-L35)、[csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L142)）。

**连带简化：端点标签不再需要「推导」**。既然边文件由 schema 列出，`src_label`/`dst_label` 就是**必填字段**（或写在端点对象里的 `label`），上一版的四级推导链（配置 → 表头 `(Group)` → 列名 `Person.id` 前缀 → 文件名约定）**整条删除**，§7.3 只剩「schema 里写什么用什么」。

**JSON 结构**：顶层按**图元素**分组（`labels` = 顶点标签 → 文件列表，`relationships` = 边类型 → 文件列表），标签名/边类型名**就是配置的键**，不再需要任何推导。完整可用的 sf0.1 配置见 [ldbc-sf01.schema.json](ldbc-sf01.schema.json)（8 个标签、23 个边文件）。

> JSON 无注释语法，所以示例用 `//` 仅作说明；正式文件里用顶层 `"_comment": [...]` 数组承载说明（loader 忽略未知顶层字段与 `_` 前缀字段）。

```jsonc
{
  "_comment": ["LDBC sf0.1 loader schema"],
  "date_format": "epoch_ms",

  // 键 = 顶点标签。值为该标签的文件列表（同一标签可以由多个文件供给）
  "labels": {
    "person": [
      { "file": "dynamic/person_0_0.csv",
        "pk": "id",
        "columns": { "id": "INT64", "firstName": "STRING", "lastName": "STRING",
                     "gender": "STRING", "birthday": "INT64", "creationDate": "INT64",
                     "locationIP": "STRING", "browserUsed": "STRING",
                     "language": "STRING", "email": "STRING[]" } }
    ],

    "place": [
      { "file": "static/place_0_0.csv",
        "pk": "id",
        "columns": { "id": "INT64", "name": "STRING", "url": "STRING" },
        // label.header：该列的值成为本行的**额外标签**（行级标签）。
        // 这里 place.csv 的第 4 列 type 取值 country/city/continent，
        // 于是每个顶点带 [place, <列值>] 两个标签；type 列不写进 columns → 不成为属性。
        //   {"header": "type"}                        原样使用列值
        //   {"header": "type", "case": "capitalize"}  → Country/City/Continent（对齐 neo4j）
        //   {"derived": ["country","city","continent"]} 不解析列值，直接枚举
        "label": [{ "header": "type" }] }
    ],

    "organisation": [
      { "file": "static/organisation_0_0.csv",
        "pk": "id",
        // 同一列 type 在这里既作行级标签、也保留为属性（属性名 kind，用 header 指回来源列）
        "columns": { "id": "INT64", "kind": { "header": "type", "type": "STRING" },
                     "name": "STRING", "url": "STRING" },
        "label": [{ "header": "type" }] }
    ]
  },

  // 键 = 边类型。src_label/dst_label 必填（端点标签不再推导）
  "relationships": {
    "knows": [
      { "file": "dynamic/person_knows_person_0_0.csv",
        "src": "Person.id", "dst": "Person.id",
        "src_label": "person", "dst_label": "person",
        "columns": { "creationDate": "INT64" } }
    ],

    "isPartOf": [
      { "file": "static/place_isPartOf_place_0_0.csv",
        "src": "Place.id", "dst": "Place.id",
        "src_label": "place", "dst_label": "place" }
    ]
  }
}
```

**这个结构带来的三个直接好处**：

1. **标签/边类型只有一个来源**：就是 JSON 的键，不再有「文件名约定 → 表头 `(Group)` → 列名前缀」的推导链，也不会有"同一标签被两个文件用不同名字声明"的可能；
2. **一个标签多文件是天然的**：值就是数组（LDBC 的 `isLocatedIn` 来自 4 个文件、`hasTag` 来自 3 个文件），多文件之间属性按**属性名**合并去重；
3. **失败模式收敛**：文件不存在 / 文件未声明 / 列漏声明 / 类型冲突，全部在启动时的配置校验阶段一次性报出（见下）。

**同一标签的多文件之间的一致性要求**（启动时校验）：

- `pk` 的**属性名**必须一致（声明了主键时）；来源列名可以不同（不同文件列名不同是常态），但类型必须相同；
- 同一属性名在两处声明了不同类型 → 报错；
- 同一属性名在两处指向了同一个文件的同一列 → 报错。

字段说明：

| 字段 | 含义 |
|---|---|
| `labels` | **键 = 顶点标签名**（这就是要创建的标签），值为该标签的**文件列表** |
| `labels.<label>[].file` | 相对 `--data-dir` 的数据文件路径（必填） |
| `labels.<label>[].pk` | 可选。该标签的主键：**属性名**（`columns` 的键）或属性名**数组**（复合主键，顺序即元组顺序，§5.4）。不写 = 该标签无主键 |
| `labels.<label>[].columns` | **属性名 → 列声明**（键 = 属性名；来源列用 `header`，缺省与键同名） |
| `labels.<label>[].label` | 可选。行的**额外标签**来源：`{"header": "列名", "case": …}`、`{"column": n}` 或 `{"derived": [...]}`；不写则该文件的行只带本键声明的标签 |
| `relationships` | **键 = 边类型名**（这就是要创建的边类型），值为该边类型的**文件列表** |
| `relationships.<type>[].file` | 数据文件路径（必填） |
| `relationships.<type>[].src` / `.dst` | 端点**列名**：`"列名"` 或 `{"列名": {"type":…, "label":…}}`（列名，不是属性名 —— 这里读的是本文件的列值；端点列不成为边属性，也不必写进 `columns`） |
| `relationships.<type>[].src_label` / `.dst_label` | **必填**：端点顶点标签，必须在 `labels` 中声明过且该标签声明了主键；也可写在端点对象里的 `label` |
| `relationships.<type>[].columns` | 边属性：规则同 `labels.<label>[].columns` |
| `date_format` | 时间列的解析格式**全局默认**：`epoch_ms`(默认) / `epoch_s` / `epoch_us` / `epoch_ns` / `iso`；文件级可覆盖 |
| `ignore` | 属于本数据集但**不参与初始装载**的文件（相对 `--data-dir`）；用于 LDBC 的 `updateStream_*.csv` 等更新流，避免被「文件未声明」校验拦下 |
| `_comment` 及其它 `_` 前缀键 | 忽略（承载说明文字） |

**字段写法参考卡（按「图语义」与「文件」两层划分）**：

| 位置 | 描述的是 | 键 / 值 | 可选字段 |
|---|---|---|---|
| `columns` | **图属性** | 键 = **属性名**；值 = 类型（短形式）或 `{"header": 列名, "type": …}` | `header`、`index`、`type` |
| `pk` | **主键由哪些属性构成** | **属性名**（字符串）或属性名数组（复合键，顺序即元组顺序） | — |
| `label` | **文件的一列**（行级标签来源） | `{"header": 列名}` / `{"column": n}` / `{"derived": [...]}` | `case` |
| `src` / `dst` | **文件的一列**（端点主键值来源） | 列名（字符串）或 `{"列名": {…}}` | `type`、`index` |

> **一句话规则**：
> **`columns` 与 `pk` 用「图里的属性名」；`src` / `dst` / `label` 用「CSV 里的列名」。**
> 因为前两者在描述**图**（有哪些属性、主键是哪几个），后两者在描述**文件**（从哪一列读值）。
> 属性 ↔ 列的对应关系**只在 `columns` 里写一次**，`pk` 不再重复；`header` 在任何位置都只表示 CSV 物理列名。

**命名规则（重要）**：`header` 一律指 **CSV 里的物理列名**；属性名指 **图里的属性名**（`columns` 的键、`pk` 的元素）。两者**没有任何隐式绑定关系** —— 用哪个列名、叫什么属性名，全部由配置决定（键的含义见上方参考卡）。

- **标签来源与属性声明是两个独立通道**：`label.header` 说的是「从哪一列读标签值」，`columns` 说的是「哪些列成为属性、各叫什么名字」。同一列可以同时走两个通道，也可以只走一个；
- `columns` 的键**就是属性名**；来源列由 `header` 指定，**缺省 = 与键同名**。于是 `"id": "INT64"` 表示「属性 `id`，取自同名列」，`"kind": {"header": "type"}` 表示「属性 `kind`，取自 `type` 列」；
- **`pk` 只写属性名**，不写列名也不写类型 —— 列由 `columns` 决定，类型也由 `columns` 决定：「属性 `userId` 是主键」就写 `"pk": "userId"`，它从哪一列来、什么类型，全看 `columns` 里那条声明；复合主键写数组 `"pk": ["tenantId", "userId"]`（顺序即元组顺序）；
- 列**只作标签来源**：写了 `label.header` 但不写进 `columns`（如 sf0.1 的 `place.type`），它就不成为属性；
- 同一物理列也可以**既作标签来源、又以改名后的属性保留**：`"columns": {"kind": {"header": "type"}}` + `"label": [{"header": "type"}]` → 值 `company` 成为标签 `company`、同时以属性 `kind` 存下来（sf0.1 的 `organisation.type` 就是这么配的）；
- `label` 也可用 `label.column`（0-based 列号）定位，用于表头不可靠/重名的文件；
- `header` 与 `index` 二者取一，`index` 用于同名表头列（`Person.id\|Person.id`）——此时用 `index` 而不是 `header` 定位。

```jsonc
"columns": {
  "creationDate": "INT64",                                  // 属性 creationDate ← 同名列
  "length":       { "type": "INT64" },                      // 同上，显式对象形式
  "kind":         { "header": "type", "type": "STRING" },   // 属性 kind ← type 列（改名）
  "personId":     { "header": "Person.id", "type": "INT64" },   // 属性 personId ← Person.id 列
  "thirdValue":   { "index": 2, "type": "INT64" }           // 同名表头列用 index 定位
}
```

**「边读哪列」≠「边用什么标签」≠「属性叫什么名」** —— 三者分别由 `label.header`、标签值本身（或 `case`）、`columns[].name` 决定，互不覆盖。行级标签的取值来自列的值（如 `company`），**不会**因为列被重命名为 `kind` 而变成 `kind`；想让标签名与属性名一致或另起名字，都是各自改各自的配置。

> **关于「能不能完全不看表头」**：可以（`label.column` + `columns` 里的 `index` 全按列号），但**不推荐**把 `header` 与 `index` 混用 —— 一旦表头与实际列序不一致（拿错文件、列被插到中间），按名匹配的列会静默错位到隔壁列。推荐做法是「属性按 `header` 名匹配（有告警/报错兜底，见 §3 的列数校验），仅在重名或表头缺失时才用 `index`」。

**配置校验（启动即查，冲突必须报错而不是取其一）**：

| 冲突 | 例子 | 处理 |
|---|---|---|
| `columns` 里两个属性指向同一列 | `{"a": {"header": "x"}, "b": {"header": "x"}}` | 报错（同一列被声明为两个属性）。**例外**：边的 `src` 与 `dst` 指向同一列是合法且常见的（自指边，如 `person_knows_person`、`place_isPartOf_place`） |
| 属性名与 `pk` 属性名重名但指向不同列 | `pk` 用 `id`（列 `id`），`columns` 里又有 `"id": {"header": "uuid"}` | 报错（主键属性必须是它实际读取的那一列，不允许解耦） |
| `header` 在表头中不存在（且未给 `index`） | `{"kind": {"header": "typo"}}` | 报错并列出该文件实际表头（strict 下必然报错；非 strict 下同样报错——这是配置错误而非数据问题） |
| 同一属性名在同一文件重复出现 | JSON 对象键重复 | 由 JSON 解析器报错（键=属性名后，这类冲突天然被 JSON 语法拦住） |

**为什么边的端点用对象而属性列用「列名 → 声明」的映射**：合法 JSON 不能有重复键（`Person.id|Person.id` 那种重名列无法用对象表达），所以 `src`/`dst` 显式命名、其余属性列用按名匹配的对象；重名列只出现在 src/dst 位置，程序可按列号定位（loader 已有「按列号定位 src/dst + 按名匹配属性」的实现，见 [csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L836-L891)）。

**为什么必须支持重命名（不只是好看）**：本仓语料就撞上了语义冲突 —— `place_0_0.csv` 的 `type` 列是**行级标签来源**（值 `country/city/continent`），`organisation_0_0.csv` 的同名列却是**公司/大学的 kind**（值 `company/university`）。若列名即属性名且不可改：

- `place.type` 会存一份与标签值完全重复的属性（`city` 顶点带 `type = "city"`），既是冗余也容易误解；
- 更糟的是没法表达"这一列只用来定标签、不要当属性"，也没法把 `organisation.type` 改成更贴切的名字。

所以 `columns` 是**白名单**：写进去的列才成为属性，写法可带 `name` 重命名；没写进去的列只在被 `label`/`pk`/`src`/`dst` 引用时才读。这同时解决了「标签列被误当属性」的问题。

**端点标签是必填配置**（`src_label`/`dst_label`，或写在端点对象里的 `label`）。因为边文件由 schema 列出、标签也由 schema 定义，端点指向哪个标签是**已知信息**，不需要也不应该靠推导：

```jsonc
"relationships": {
  "knows": [ { "file": "dynamic/person_knows_person_0_0.csv",
               "src": "Person.id", "dst": "Person.id",
               "src_label": "person", "dst_label": "person" } ]
}
```

- 不提供默认值。缺 `src_label`/`dst_label` 即**启动时报错**（而不是运行期整文件跳过边）——这正是现状最坑的失败模式（[csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L825-L830) 在找不到 group 时静默跳过整个边文件）；
- 启动时校验 `src_label`/`dst_label` 必须是 `labels` 里声明过的键，且该标签声明了主键（无主键的标签无法被边引用）；
- 端点对象内也可写 `label`（等价写法），例如 `"src": { "Person.id": { "type": "INT64", "label": "person" } }`；两种写法同时给出且不一致则报错。

**生成配置时注意两个坑**（都由语料实测暴露，配置模板已按此处理）：

1. **边文件只写 `src`/`dst` 会漏掉边属性**：`person_hasInterest_tag_0_0.csv` 等 8 个文件只有两列，而 `forum_hasMember_person_0_0.csv` 有 `joinDate`、`person_studyAt_organisation_0_0.csv` 有 `classYear`……**这些属性列必须出现在配置里**，否则它们会走 ④ 采样推断（`classYear` 恰好是纯数字侥幸推断正确，纯数字的字符串列就会被误判成 INT64）。生成模板时已逐文件核对，23 个边文件全部显式声明。
2. **不要为了省事把 `columns` 省掉**：一旦 `--schema` 存在就默认 strict（见上），漏列会直接报错而不是静默推断——这正是我们要的失败行为。

**实现载体**：JSON 解析用仓库已有的 `nlohmann-json`（vcpkg.json 已声明、`src/service/thrift/result_format.cpp` 已使用），**不新增第三方依赖**；`SchemaConfig` 与解析逻辑目前在 `csv_loader.{hpp,cpp}` 内（后续可拆出 `schema_config.*`，属代码组织优化，不影响接口）。

**命令行只剩「运行方式」**（声明全部在 schema 文件里）：

```bash
--schema <file.json>                                 # 必填：唯一的声明入口
--data-dir <dir>                                     # 必填：schema 里 file 的基准目录
--host 127.0.0.1 --port 9090                         # 连哪个服务端
--batch-size 2000 --rpc-connections 2 --parallel-files 2
--no-schema-strict                                   # 未声明的列回落到采样推断（默认 strict：报错并列出列名）
```

> **已删除**：`--pk` / `--types` / `--date-format` / `--delimiter` / `--skip-undeclared-check`。
> 前四个是**数据声明**，进 schema（见下）；最后一个的等价物是 schema 的 `undeclared_files`。
> 删除后能力无损失：属性改名由 `columns` 的 `header`/`index` 承担，类型与主键本来就在 JSON 里。

**strict 语义**（已实现并有用例）：默认要求每个 CSV 列都被 `columns` / `pk` / `label` / `src` / `dst` 覆盖，
否则**报错并列出列名**（`column(s) not declared: [a, b]`），不会静默丢弃；
`--no-schema-strict` 时对未声明列按前 200 行采样推断（全整数 → INT64，否则 STRING）并打 INFO 日志。
同名表头列在推断模式下会被跳过并告警（列名无法唯一标识来源，需显式声明）。

**schema 顶层字段**

| 字段 | 含义 |
|---|---|
| `delimiter` | 字段分隔符**全局默认**（默认 `"|"`；支持多字符，如 `"::"`） |
| `date_format` | 时间列解析格式**全局默认**（默认 `"epoch_ms"`；可选 `epoch_s` / `epoch_us` / `epoch_ns` / `iso`） |
| `undeclared_files` | 数据目录里有「未声明 CSV」时的行为：`"error"`（默认，报错列出文件）或 `"ignore"` |
| `ignore` | 属于本数据集但**不参与初始装载**的文件列表（如 LDBC 的 `updateStream_*.csv`） |

**文件级覆盖**（与 `columns` / `pk` / `label` 同一套分层：全局默认 → 文件级覆盖）

```jsonc
{
  "delimiter": "|",              // 全局默认
  "date_format": "epoch_ms",     // 全局默认
  "labels": {
    "Person": [{
      "file": "dynamic/person_0_0.csv",
      "pk": "id",
      "columns": { "id": "INT64", "creationDate": "DATETIME" }
    }],
    "LegacyPerson": [{
      "file": "legacy/person.csv",
      "delimiter": ";",          // 本文件用分号
      "date_format": "epoch_s",  // 本文件的时间列是秒
      "pk": "id",
      "columns": { "id": "INT64", "creationDate": "DATETIME" }
    }]
  }
}
```

> 一处例外要记住：**时间列的解析必须按文件取值**（`resolveDateFormat(config, fs)`），
> 因为同一批 CSV 里不同文件的时间单位可能不同；分隔符同理（`resolveDialect(config, fs)`）。

**类型表**

| 配置写法 | CsvColumnType | PropertyType | CSV 值解析 |
|---|---|---|---|
| `BOOL` | BOOL | BOOL | `true/false` |
| `INT` / `INT64` / `LONG` | INT64 | INT64 | `stoll` |
| `DOUBLE` | DOUBLE | DOUBLE | `stod` |
| `STRING` | STRING | STRING | 原样 |
| `INT[]` / `LONG[]` | INT64_ARRAY | INT64_ARRAY | `;` 拆分后逐项 `stoll` |
| `DOUBLE[]` | DOUBLE_ARRAY | DOUBLE_ARRAY | `;` 拆分后逐项 `stod` |
| `STRING[]` | STRING_ARRAY | STRING_ARRAY | `;` 拆分 |
| `DATE` | DATE | DATETIME (kind=DATE) | 见下 |
| `DATETIME` / `DATETIME_WITH_TZ` | DATETIME | DATETIME (kind=LOCAL_DATETIME / DATETIME) | 见下 |
| `TIME` / `TIME_WITH_TZ` | TIME | TIME (kind=LOCAL_TIME / TIME) | 见下 |
| `DURATION` | DURATION | DURATION | `P...` ISO-8601 |
| 上述 + `[]` | *_ARRAY | *_ARRAY | `;` 拆分后逐项解析 |

未声明类型时：给了 `--schema` 就默认 strict → **报错指出文件与列名**；没给 `--schema`（或显式 `--no-schema-strict`）则回落到 ③④⑤，且每个被推断的列打一条 INFO 日志，便于事后核对。

---

## 5. 时序类型的落地细节（口径 7）

### 5.1 解析器必须先从匿名命名空间提出来

现有的 ISO-8601 / 组件式解析器**只在 `temporal_functions.hpp` 的匿名命名空间里**（[temporal_functions.hpp](../../../src/query/function/scalar/temporal_functions.hpp#L22-L27)、[L612](../../../src/query/function/scalar/temporal_functions.hpp#L612) `parseDatetimeStr`、`parseDateFromString`、`parseTimeStr`、`parseDurationFromString`），loader 无法复用。**若在 loader 里另写一份解析器，就违反了「同类失败禁止逐条特判 / 统一机制」的红线**。

因此第一步是一次**小重构**：把它们移入 `common/types/temporal_value.hpp` 的公开 API（`parseDateTimeString` / `parseDateString` / `parseTimeString` / `parseDurationString`），`temporal_functions.hpp` 改为调用它。判据：TCK 时间语义用例（[tck-guide.md](../../tests/tck-guide.md)）全部保持通过 —— 这是纯搬家，行为不允许变化。

### 5.2 解析规则

| 配置类型 | 接受的值 |
|---|---|
| `DATE` | ISO 日期（`1989-12-03`，含扩展/周/序数形式）或 epoch 数值 → `kind=DATE` |
| `DATETIME` | ISO 本地时间（`2011-08-17T12:34:56.789`）或 epoch 数值 → `kind=LOCAL_DATETIME` |
| `DATETIME_WITH_TZ` | ISO 带偏移/时区名（`2011-08-17T12:34:56+02:00`、`...Europe/Paris`）→ `kind=DATETIME` |
| `TIME` / `TIME_WITH_TZ` | ISO 时间（`12:34:56.789`）或 epoch 数值 → `kind=LOCAL_TIME` / `TIME`（带时区的类型必须带偏移或时区名） |
| `DURATION` | ISO-8601 duration（`P1Y2M3DT4H5M6S`） |

**epoch 数值**：按 `date_format` 解释（默认 `epoch_ms`）。

> **语料与模板的取舍**：本仓语料的 `birthday` / `creationDate` 是 **LONG 毫秒**（如 `628646400000`、`1266161530447`），文件名 `...-LongDateFormatter` 也表明如此。因此 [ldbc-sf01.schema.json](ldbc-sf01.schema.json) 把这些列声明为 **`INT64`** —— 这是**有意选择**（保真、且 LDBC 对比脚本按数值比较），不是"还没支持时序"。想以时间类型装载时改成 `{"birthday": "DATE", "creationDate": "DATETIME"}` 并保留 `date_format: epoch_ms` 即可，两条路径都不影响 G1/G2 的验证。

### 5.3 非法值的失败行为

CSV 里的解析失败（`std::stoll("abc")`、非法 ISO）当前会抛 `std::invalid_argument` 且**不带上下文**。本次统一为：**报错信息一定包含 `文件:行号:列名:原始值`**，并在报 `--max-parse-errors`（默认 1）条后退出，避免整库静默错值。这属于「对导入文件的解析变更必须考虑畸形输入」的安全要求。

### 5.4 复合主键

**结论：本次一并支持。** 存储与索引层原生支持复合键，不需要任何新机制：

| 环节 | 现成能力 |
|---|---|
| 索引定义 | `LabelDef::IndexDef.accessors` 是**有序**向量，`ON (n.a, n.b)` 已被 [index_ddl_parser.cpp](../../../src/query/parser/index_ddl_parser.cpp#L52-L88) 解析（`readPropertyList` 逗号分隔、保序） |
| 唯一索引 | `CREATE UNIQUE INDEX ... ON (n.id, n.email)` 已有 e2e 用例（[test_index_e2e.cpp](../../../tests/test_index_e2e.cpp#L1086)） |
| 条目编码 | `IndexKeyCodec::encodeIndexKey(values, vid)` 按序拼接各列的可排序编码 + vid 后缀 |
| 点查 | `scanVerticesByIndexId(index_id, values)` 传完整元组即等值命中 |

所以打通三处即可：**配置语法**（§4 决定 3）、**Thrift**（`PkKey.keys` 本就是列表）、**服务端解析**（按元组查索引）。

**配置语法**：`pk` 只写**属性名**（`columns` 的键），单列用字符串、复合用数组（数组顺序即元组顺序）。它从哪一列取值、什么类型，**全部由 `columns` 里那条声明决定**，`pk` 不重复写 —— 这消除了「同一列在两处被描述、可能互相矛盾」的一整类配置错误。完整可跑示例见 [composite-key-example.schema.json](composite-key-example.schema.json)（多租户 `(tenantId, userId)` 场景）：

```jsonc
"labels": {
  "user": [ {
    "file": "users.csv",
    "pk": "userId",                                     // 单列：属性名
    "columns": { "userId": { "header": "id", "type": "INT64" }, "name": "STRING" }
  } ],

  "membership": [ {
    "file": "memberships.csv",
    "pk": ["tenantId", "userId"],                       // 复合：属性名数组，顺序即元组顺序
    "columns": {
      "tenantId": { "header": "tenant", "type": "STRING" },   // 列 tenant → 属性 tenantId
      "userId":   { "header": "id",     "type": "INT64"  }
    }
  } ]
}
```

规则：

1. 数组**非空**，元素不重复（同一属性出现两次即报错）；
2. 每个元素**必须是 `columns` 里声明过的属性**（`pk` 引用属性名，未声明的属性即配置错误）；
3. 该属性**必须显式声明类型**（`"userId": {"header": "id"}` 这种没给 `type` 的写法，若被 `pk` 引用则报错 —— 主键类型不允许推断）；
4. `pk` 元素不接受 `header` / `index` / `type`（那是 `columns` 的职责）：写了即报错，避免两处描述同一列而互相矛盾；
5. `LabelDef.pk_prop_ids` = 按该顺序把属性名解析成 prop_id；
6. 服务端解析端点时，`PkKey.keys` 必须**按同一顺序**给出完整元组；数量或类型对不上即报错（不做部分匹配）。

**端点侧（`src`/`dst`）仍写列名**，因为端点是「从本文件的哪一列读值」，不是本文件的属性：`"src": "Person.id"` 指该列的值作为 `src_label` 的主键值发出去。它也不需要出现在 `columns` 里（端点列不成为边属性）。

**对应索引**（§6.3 的规则自然扩展）：声明复合主键时，建的是**覆盖全部主键列的复合唯一索引**：

```text
CREATE UNIQUE INDEX idx_{label}_{pk1}_{pk2}..._unique FOR (n:{label}) ON (n.{pk1}, n.{pk2}, ...)
```

- 索引名含全部主键列名；若拼接后过长（超过 63 字节）则退化为 `idx_{label}_pk{index_id}_unique` 并在日志里给出实际名字（避免平台长度限制导致静默改名）；
- 复合键的**唯一性语义**与单列一致：`checkUniqueConstraint(table, values_vector)` 对整元组判重，所以 `(tenant=1,id=5)` 与 `(tenant=2,id=5)` 是两个不同顶点，互不冲突；
- 查询侧同样免费获得加速：`MATCH (n:User {tenant: $t, id: $i})` 由 `tryBoundIndexScan` 按 accessor 顺序匹配（前缀列缺失时只能用到前 k 列，与普通复合索引一致）。

**边端点**：`PkRef.keys` 是 `list<PkKey>`，复合键即多个元素，顺序必须与顶点声明一致（§4 决定 2）。loader 侧按该文件点声明的主键列顺序逐列取值与解析。

---

## 6. 服务端改动

### 6.1 DDL

```thrift
struct LabelInfo {
  1: i16 id
  2: string name
  3: list<PropertyDefThrift> properties
  4: list<string> pk_props        // 新增：主键属性名，有序（空 = 无主键）
}
service EuGraphService {
  LabelInfo createLabel(1: string name, 2: list<PropertyDefThrift> properties,
                        3: string graph_name, 4: optional list<string> pk_props)   // 新增第 4 参
}
```

`optional` 在现有生成配置下映射为 `std::unique_ptr<...>`，与 `co_batchInsertVertices` 等现有方法的「未设置即为 null」判空方式一致，handler 侧无需额外处理。

`createLabel` 处理：

1. `pk_props` 为空/缺省 → `pk_prop_ids` 为空，**不建任何额外存储**（口径 2）；
2. `pk_props` 非空 → 校验：**无重复属性名** + 每个名字都在 `properties` 中（属性名 → `prop_id`，**按给定顺序**存入 `LabelDef.pk_prop_ids`，顺序即主键元组顺序），并**校验配套索引存在**：该标签下存在 `unique == true` 的索引，其 accessor **按同一顺序**恰好覆盖这些属性（顺序不同即报错，否则等值点查匹配不上）。索引由 loader 在此次运行中先建（`CREATE UNIQUE INDEX`），DDL 层**不隐式替用户建索引**；
3. **幂等**：标签已存在且已有主键 → 比较 `pk_props`：**相同 → 空操作**（loader 每次运行都会先 `createLabel`，这是正常路径）；**不同 → 报错**，不允许改主键定义。已存在但无主键、本次带主键 → 补声明（支持先建标签后补主键；**已有顶点不会回填索引条目，必须显式跑一次 `CREATE INDEX` 回填**，日志要写清楚）；
4. 主键不引入新表，因此 `dropLabel` 只需按现状 drop 索引表即可。

### 6.2 写入路径：批量导入必须维护索引（机制改造）

**这是本次改动最关键的机制事实**：现状 `batchInsertVertices` **不维护索引**。它走的是 `SyncGraphDataStore::insertVertex`（[sync_graph_data_store.cpp](../../../src/storage/data/sync_graph_data_store.cpp#L280-L312)），只写 `vertex_existence` / `label_reverse` / `label_fwd` / `vprop_*`，索引条目由查询写路径的算子单独维护（[create_node_physical_op.cpp](../../../src/query/physical_plan/operator/create_node_physical_op.cpp#L375)、[set_physical_op.cpp](../../../src/query/physical_plan/operator/set_physical_op.cpp#L296-L297) 等）。

后果：**主键索引在装载期是空的**，只有 `CREATE INDEX` 的回填才会把它填上；而且任何"装完之后再批量导入"的数据都不会进索引（索引会静默变脏）。既然我们要靠索引解析端点、并希望它与查询共用一套机制，这条必须修：

```text
方案：把索引维护下沉为数据存储层能力
  IAsyncGraphDataStore：batchInsertVertices 增加 label_defs 参数（该端点在仓内只有 loader 一个
  调用方，直接改签名，不保留旧重载以免留下「不维护索引」的入口）
    folly::coro::Task<void> batchInsertVertices(std::vector<BatchVertexEntry> entries,
                                                const std::vector<LabelDef>& label_defs)
  实现：对每条顶点先 insertVertex(...)，再按 label_defs 计算索引条目并写入
       （唯一索引先 checkUniqueConstraint，冲突则该条不写）
```

- 复用现成 helper 的**判定语义**（`checkUniqueConstraint` → 冲突返回 false，不抛异常，正好是 first-wins 需要的钩子，[vertex_index_maintenance.hpp](../../../src/query/physical_plan/operator/vertex_index_maintenance.hpp#L207-L220)）；
- **依赖方向**：索引维护 helper 现在位于 `src/query/physical_plan/operator/`（`compute` 命名空间）。由存储层的 `batchInsertVertices` 去 include query 层头文件是反向依赖，因此必须把「收集条目 / 唯一性检查 / 写条目」的逻辑下沉到 `src/storage/data/`（例如 `vertex_index_maintenance.hpp` 移为存储层组件），query 算子改为调用新位置 —— 行为不变，属 AGENTS.md 要求的依赖方向单一化；
- 影响面：`batchInsertVertices` 目前**只有 loader 使用**（已确认，§8）。改造后任何批量导入都会顺带维护索引，"索引变脏"这一类问题一次性消除。
- **同类问题一并覆盖**：`batchInsertEdges` 同样不维护边索引（同一段代码只调 `insertEdge`）。本次一并把边索引维护下沉并接上，否则「批量导入的边搜不到」会在下一步暴露 —— 属 AGENTS.md「同类失败禁止逐条特判、优先补齐统一机制」。

`GraphService::batchInsertVertices`（[graph_service.cpp](../../../src/service/graph_service.cpp#L297-L340)）：

```text
1. 解析主标签/附加标签 id（不变）
2. 若该标签声明了主键：
   a. 取主键索引 index_id
   b. 对本批候选主键做唯一性预检（checkUniqueConstraint / 索引点查）
      - 已存在 → first-wins：跳过该记录、计入 duplicate_pk、WARN（口径 3）
      - 批内重复 → 同样 first-wins
3. 对存活记录 nextVertexIdRange + 构造 BatchVertexEntry
4. 一次调用 batchInsertVertices(entries, label_defs)：顶点与索引条目同事务写入
5. 返回 inserted / duplicate_pk / vertex_ids
```

`GraphService::batchInsertEdges`：

```text
1. 解析边标签 id（不变）
2. 收集本批端点，按 (label_id, pk_name) 分组
3. 每组 → 主键索引点查（scanVerticesByIndexId）
   - 未解析端点 → 该边跳过（skipped_unresolved）
   - 同组命中多条 → 取 vid 最小者 + WARN（first-wins，不报错）
4. 可解析的边：nextEdgeIdRange(count) + batchInsertEdges（不变）
5. 返回 BatchInsertEdgesResult
```

**并发**：装载期可用多条 RPC 连接并发写（现状能力保持）。索引条目的并发写入由 WT 事务保证；first-wins 语义下**并发写入同一主键值可能双写**（两个连接同时预检、都没命中）——索引 key 末尾带 vid，两条条目会共存，后续解析命中「同标签同主键值多条」。处置：解析时同标签内命中多条 → 取 **vid 最小者**并 WARN，不报错、不阻断装载。

> 可选开关（非本次主线，实现时可一并带上）：`--on-duplicate-pk=error|skip`。`error` 让 loader 在主键冲突时直接失败——调试脏数据时比 first-wins 更快定位；默认值仍为 first-wins，保持与现状一致。

### 6.3 唯一索引（已确认必须创建）

**口径已定：声明了主键的标签，必须创建该列的唯一索引 —— 这条索引就是主键本身。** 服务端解析端点、查询点查加速都用它，不存在第二份存储（§4 决定 1）。

```text
CREATE UNIQUE INDEX idx_place_id_unique FOR (n:place) ON (n.id)
CREATE UNIQUE INDEX idx_membership_tenantId_userId_unique FOR (n:membership) ON (n.tenantId, n.userId)   // 复合键
```

- filter label 就是 **schema 的键**（`labels` 的键），accessor 是主键列，**弱 accessor**（按属性名解析）；
- 一条索引即覆盖该标签的全部顶点：schema 键是主标签，**行级 `label.header` 产生的额外标签不影响覆盖面** —— 例如 `place` 文件里 `:LABEL=city` 的顶点带 `[place, city]` 两个标签，弱 accessor 会从 `city` 的 vprop 表里按属性名取到 `id`，条目照样进 `(n:place)` 这条索引；
- 因此 `MATCH (n:city {id: 5})` **仍然能命中** `place` 上的索引（规划器按 `city` 标签找不到索引时会回退为标签扫描 + 过滤，结果正确）；想让 `city` 也直接走索引，可另建一条 `(n:city) ON (n.id)`（可选，非必需）。
- **列顺序 = 主键元组顺序**，与 `pk_prop_ids` 必须一致（顺序错了等值点查就匹配不上，§5.4）。

**废弃现有实现的两处**：

1. 现在「取 `schema.properties[0]` 当主键列」的隐式假设（[csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L755-L763)）；
2. 现在「按映射标签名 + 行级标签各建一条索引」的做法 —— 主标签已统一为 schema 键，不再需要区分。

**建索引与批量写入的先后顺序**：因为 §6.2 的改造让批量写入维护索引，顺序变为「先建索引 → 再写顶点（顺带写索引条目）→ 再写边」。这样装载期索引始终可用，`CREATE INDEX` 只需处理极少数历史数据（或退化为空操作），也避免了"先写数据后建索引、期间的写入漏进索引"的窗口。

### 6.4 行级标签（原「派生标签」）

行级标签由 `label` 字段声明，**取值仍是"指定即所用"**：

```jsonc
// static/place_0_0.csv 的第 4 列 type 取值 country / city / continent
"place": [ { "file": "static/place_0_0.csv", "pk": "id",
             "columns": { "id": "INT64", "name": "STRING", "url": "STRING" },
             "label": [{ "header": "type" }] } ]                      // 原样：country/city/continent
// 或
"label": [{ "header": "type", "case": "capitalize" }]                 // 归一化：Country/City/Continent
// 或
"label": [{ "derived": ["country", "city", "continent"] }]             // 不解析列值，直接枚举
```

**与上一版的关键差别（本轮简化后更清晰）**：

| | 上一版 | 本轮 |
|---|---|---|
| 主标签 | 行级标签**优先成为主标签**，属性存在 `city` 下 | **主标签 = schema 的键**（`place`），属性永远存在它下面 |
| 行级标签的角色 | 决定属性归属与索引归属 | 只**追加**一个额外标签（`[place, city]`），便于按 `City/Country/...` 查询 |
| 索引 | 建在文件级标签上，与属性归属错位 | 建在 schema 键上，与属性归属一致 |
| `:LABEL` 空值 | 忽略该行级标签 | 同（忽略，只保留 schema 键） |

这样就没有"属性在哪、索引在哪、查询写哪个名字"三者错位的问题：**属性、索引都属于 schema 键，行级标签只是额外可见性**。

> 与 neo4j 的差异：neo4j 那边 `sed` 把 `:LABEL` 值改成首字母大写、并把属性放在该标签下；我们用 `case: capitalize` 得到同样的标签名，但属性始终在 `place` 下。查询写 `MATCH (n:City)` 两边都通（顶点带该标签），但 `MATCH (n:City {name:...})` 的属性解析按各自引擎的规则走。

---

## 7. Loader 改动

### 7.1 数据结构

```cpp
// 删除（CLI 映射模式与目录扫描一并移除）
using CsvIdMap = std::unordered_map<std::string, std::unordered_map<int64_t, uint64_t>>;
struct LoadedIdMaps { CsvIdMap group_id_map; std::unordered_map<std::string, std::string> label_to_group; };
std::vector<CsvFileInfo> scanCsvFiles(const std::string& data_dir);      // 目录扫描
bool parseNodeSpec / parseRelationshipSpec(...);                        // --nodes / --relationships

// CsvFileInfo 简化：只留 schema 需要的字段
struct CsvFileInfo {
    std::filesystem::path path;
    std::string label;                 // 顶点标签（取自 schema 的键）或边类型（同上）
    bool is_vertex = false;            // 由它出现在 labels 还是 relationships 下决定
    // 删除：label 列表推导 / src_label / dst_label / edge_type / stem 约定
};

// 列声明：CSV 表头列 → 图属性（同一套语法用于 columns / pk / src / dst）
struct ColumnSpec {
    std::string header;              // CSV 物理列名（或空，用 index 定位）
    int index = -1;                  // 0-based 列号（与 header 二选一）
    std::string name;                // 属性名（缺省 = header）
    CsvColumnType type;
    bool typed = false;              // 是否显式声明了类型
};
using PkProps = std::vector<std::string>;        // 主键属性名，有序 = 元组顺序（§5.4）；来源列/类型查 columns

// schema 的结构 = JSON 的结构（键即标签/边类型）
struct NodeFileSpec {
    std::filesystem::path file;
    std::optional<PkProps> pk;                       // 未声明 = 该标签无主键；元素必须是 columns 的键
    std::vector<ColumnSpec> columns;                 // 属性白名单（键=属性名）
    std::optional<LabelSource> extra_label;          // label.header / column / derived
};
struct EdgeFileSpec {
    std::filesystem::path file;
    ColumnSpec src, dst;
    std::string src_label, dst_label;                // 必填
    std::vector<ColumnSpec> columns;
};
struct SchemaConfig {
    std::map<std::string, std::vector<NodeFileSpec>> labels;          // 标签 → 文件列表
    std::map<std::string, std::vector<EdgeFileSpec>> relationships;   // 边类型 → 文件列表
    CsvDateFormats date_format = CsvDateFormats::EPOCH_MS;
};

// 装载上下文：只有「标签 → 主键属性名」
struct PkNameMap {
    std::unordered_map<std::string, std::string> pk_name_by_label;   // label -> "id"
};
```

**`LoadedIdMaps` 的替代物是「标签 → 主键名」**（8 个标签 → `"id"`）：内存从 O(顶点数) 降到 O(标签数)。这是 G1 的直接体现。

### 7.2 顶点装载

```text
for (label, files) in schema.labels:
  for file_spec in files:
    每行 → VertexRecord {
      labels     = [label] + extra_label(该行取值)        // label 是 schema 的键；extra_label 即 label.header/derived
      properties = 按 label 的属性顺序排列的类型化值        // 属性名来自声明（键=属性名），不是表头原文
      pk         = file_spec.pk 逐【属性名】查 columns 得到 (来源列, 类型) → 取值
                   → [{ name, parse(fields[col], type) }, ...]   // 顺序即元组顺序
    }
    批量 RPC batchInsertVertices(label, records, graph)
  // 不再收集/回传任何 csv_id -> vid
```

- 属性顺序按**该标签**合并后的属性定义排（多个文件供同一标签时按属性名去重合并）；
- `labels` 的顺序 = `[schema 键] + [extra_label]`，即文件级标签在前、行级标签在后（与现状一致）；**不再有"行级标签优先成为主标签"** 这一规则 —— 主标签就是 schema 的键，属性永远存在它下面，语义唯一；
- 无 `pk` 的文件照常导入，只是它的顶点不能被边引用（§2 口径 1）。

`createLabel` 调用时带上该标签的 `pk_props` —— 就是配置里的属性名数组（按元组顺序；未声明则空），无需任何名字到列的转换。点装载结束后**为每个实际承载主键属性的标签**建唯一索引（§6.3）：

```text
targets = schema.labels 的键中「声明了主键」的那些   // 不再需要剔除命名空间标签：主标签就是键本身
for label in targets:
    CREATE UNIQUE INDEX idx_{label}_{pk1}[_{pk2}...]_unique
        FOR (n:{label}) ON (n.{pk1}[, n.{pk2}...])       // 列顺序 = 主键元组顺序
```

这比上一版更简单也更正确：上一版把「属性落在行级标签（`city`）下、索引却建在文件级标签（`place`）上」当成缺陷来修，现在两者统一到 schema 的键上，`place` 索引天然覆盖 `place` 标签下的所有顶点（含行级标签为 `city` 的那些）。

当前实现「取 `schema.properties[0]` 当主键列」的隐式假设（[csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L755-L763)）随之删除。

### 7.3 边装载

```text
for each edge file:
  src_col/dst_col     = 表头 :START_ID/:END_ID 优先，否则 schema 的 src/dst.header，否则第 0/1 列
  src_primary_label   = 配置 src_label/src.label → 表头 (Label) → 列名 Label.id 前缀 → 文件名约定（§4 决定 3）
  src_pk_name         = pk_name_by_label 查找（规则见下）
  src_pk_type         = 声明类型（缺失时按 INT64 兜底并 WARN）
  每行 → EdgeRecord {
    properties = 按 schema.properties 名匹配的列值                                 // 不变
    src = PkRef{ primary_label = src_primary_label, keys = [{ src_pk_name, parse(...) }] }
    dst = PkRef{ ... }
  }
  批量 RPC batchInsertEdges(...)
  汇总服务端返回的 inserted / skipped_unresolved
```

`pk_name_by_label` 在点装载阶段建立（键=标签名，值=该标签的主键属性名），边装载时直接按 `src_label` / `dst_label` 查表：

| 情况 | 处理 |
|---|---|
| `src_label` 在 schema 的 `labels` 里，且声明了主键 | 正常取主键名与类型 |
| `src_label` 未声明主键（标签存在但无 `pk`） | 报错：**该边引用了一个不可解析的标签**（无主键就无法定位顶点）。要么给该标签声明主键，要么修正 `src_label` |
| `src_label` 不在 schema 的 `labels` 里 | 启动时配置校验即报错（拼错标签名是最常见的配置错误） |
| 边两端指向同一列（自指边，如 `person_knows_person`） | 合法，正常处理 |

> 上一版设计里的四级标签推导（配置 → 表头 `(Group)` → 列名 `Person.id` 前缀 → 文件名约定）已全部删除：边文件由 schema 列出，端点标签是配置里的显式字段，不需要也不应该再猜。

### 7.4 两阶段顺序

保持「先点后边」。差别在于第二阶段不再依赖 loader 内存映射，而依赖服务端索引 —— 这使「点文件分批多次运行」「增量补边」成为可能（后续独立任务，本次不做）。

---

## 8. 兼容性与影响面

| 表面 | 影响 | 处置 |
|---|---|---|
| Thrift 写协议（`VertexRecord`/`EdgeRecord`/`batchInsert*` 返回值） | **破坏性变更，且不需要兼容**（已确认）：`src_vertex_id` / `dst_vertex_id` 直接删除、字段编号重排、新增 `PkRef`；`batchInsertEdges` 返回类型变更 | 全仓只有 `eugraph-loader` 一个调用方（已确认，见 [csv_loader.cpp](../../../src/program/loader/csv_loader.cpp#L617-L904)），server 与 loader 同版本发布；不保留 reserved 编号、不做双写 |
| 文件搬迁 | 新增 `src/program/loader/schema_config.{hpp,cpp}`；索引维护逻辑从 `src/query/physical_plan/operator/*_index_maintenance.hpp`（`compute` 命名空间）下沉到 `src/storage/data/`（§6.2） | 依赖方向单一化 + 单一职责（AGENTS.md 物理设计约束）；query 算子改为调用新位置，行为不变 |
| 元数据编码（`LabelDef`） | 新增 `pk_prop_ids` 字段 | **不需要兼容**（已确认）：`meta_codec` 直接改编码格式，不写旧格式解码分支、不做迁移测试 |
| KV 编码 | **不变**：不新增表类型，完全复用 `vidx_*` 与 `IndexKeyCodec` | — |
| Cypher 语义 | **不变**：不动 `CREATE` / `MERGE` / `MATCH` | — |
| 时序解析器位置 | `parseDatetimeStr` 等从 `function/scalar` 匿名命名空间**上提到 `common/types`** | 纯搬家 + TCK 回归；属代码组织变更，不是行为变更 |
| 行级标签 | **语义变化（简化）**：不再抢占主标签，只追加；主标签 = schema 键 | 属性与索引归属统一到 schema 键，消除「属性在 `city` 下、索引建在 `place` 上」的错位 |
| Loader 命令行 | **行为变化（刻意）**：`--nodes` / `--relationships` 与目录扫描模式**删除**；`--schema`、`--data-dir` 变为必填 | 装载入口唯一（schema 文件），少约 158 行代码与两条并行代码路径；现有调用脚本需改写：`--nodes=Place:City=static/place.csv` ⇒ `"place": [{"file": "static/place.csv", "label": [{"header": "type", "case": "capitalize"}]}]`（[loader.md](../usage/loader.md) 待同步） |
| 主键索引的 filter label | **明确**：建在 schema 键（主标签）上 + 弱 accessor，一条覆盖该标签全部顶点（§6.3） | 行级标签不影响覆盖面；`MATCH (n:place {id:X})` 走索引 |
| 批量导入路径 | **行为变化（修缺陷）**：`batchInsertVertices` 由「不维护索引」改为「维护索引」（§6.2） | 消除「批量导入后索引静默变脏」；该端点只有 loader 使用 |
| 重复主键 | **语义不变**（first-wins），现状是静默 | 新增告警日志；可选开关 `--on-duplicate-pk`（取值 `error` 或 `skip`） |

---

## 9. 性能预期

| 项 | 现状 | 本方案 |
|---|---|---|
| Loader 内存 | O(顶点数 × ~50B)：sf0.1 约数十 MB，SF1 约 GB 级 | O(标签数)，与顶点数无关 |
| 顶点写入 | 1× `insertVertex`，**不写索引** | 1× `insertVertex` + 每条 live 索引 1 次唯一性预检 + 1 次索引条目写入（同事务） |
| 边写入 | 内存 `unordered_map` 两次查找 | **每批 2×batch_size 次索引点查**（`scanVerticesByIndexId`），按 (label, pk_name) 分组后单次 `io_` dispatch 内完成 |
| 建索引时机 | 点装载**之后**建，靠回填补条目 | 点装载**之前**建，装载时顺带写条目，回填近乎空操作（§6.3） |

索引点查是前缀扫描、命中即停，属 B-tree 单页访问级别。**验收判据**：SF0.1 全量装载（点 + 边）总耗时相对现状劣化不超过 10%，且 loader RSS 峰值与顶点数解耦（顶点数 ×10 时 RSS 不随之 ×10）。若边装载成为瓶颈，优化手段（按 src/dst 排序提升局部性、批内 PK 缓存）留待实测后决定，不预先引入。

---

## 10. 测试策略

| 层级 | 用例 | 判据（必须能区分对错） |
|---|---|---|
| 单元：CSV 读取（R1 BOM） | 表头首列带 UTF-8 BOM 的文件 + 不带 BOM 的文件，各声明同一份 schema | 两者都导入成功且**属性名一致**（都是 `id`，不是 `\uFEFFid`）；缺陷存在时 BOM 那份会报「列 id 未声明」或产生幽灵属性 |
| 单元：CSV 读取（R2 引号） | 引号内含分隔符（`"a&#124;b"&#124;x`）、转义引号（`"say ""hi"""`）、引号内含换行 | 首字段 = `a&#124;b`（**不是**被切成两列）、`say "hi"`、含换行的单个字段；缺陷存在时字段被错误切分 —— 属静默数据损坏，必须能区分 |
| 单元：CSV 读取（R3 分隔符） | 同一份数据分别以 `&#124;` / `,` / `\t` / `;` / **多字符 `::`** 保存，各在 schema 里声明 `delimiter` | 结果完全相同（顶点/边数与属性值逐项相等）；多字符分隔符按**整体**匹配，不被部分切分 |
| 单元：CSV 读取（R4 列数） | 构造一行字段数比表头多一列（未转义分隔符） | 报错含 `文件:行号:表头 N 列 / 本行 M 列`，并提示加引号或换分隔符；**不得静默按位错位** |
| 单元：schema 文件校验 | `--schema` 里写一个不存在的 `file`；或语料里存在未声明的文件 | 前者启动即报错并列出缺失文件；后者报错同时指出「文件未在 schema 中声明」，与「列漏声明」可区分 |
| 单元：JSON 解析 | [ldbc-sf01.schema.json](ldbc-sf01.schema.json) 能完整解析；**文件级 `delimiter` / `date_format` 覆盖全局默认**；非法 JSON / 未知类型 / 列名不存在 / 不可用分隔符 / `pk` 与 `columns` 类型冲突 | 全局与文件级各构造一例（同一批文件用不同分隔符、不同时间单位），断言**都能正确解析**；每类非法配置的报错都含文件与字段名 |
| 单元：配置校验（唯一入口） | ① `src_label` 缺失；② `src_label` 指向未声明的标签；③ `src_label` 指向的标签未声明主键；④ schema 里的 `file` 不存在；⑤ 数据目录里有文件未在 schema 声明；⑥ 同一标签的多文件主键属性名/类型不一致 | 六种情况**全部在启动时报错**并指出具体文件/标签/字段（缺陷存在时会在运行期静默跳过边或错值，边数/属性数可判据） |
| 单元：标签值 vs 属性名 | 同一列同时作标签来源与属性：`label.header=type` + `columns.type.name=kind`；以及同一列只作标签来源（不写进 columns） | 前者：标签 = 单元格原值（`company`，**不是** `kind`），属性 = `kind`；后者：标签存在、**该列不产生任何属性**；缺陷存在时会出现「标签被改写成属性名」或「标签列变成幽灵属性」 |
| 单元：标签来源定位 | `label.header` 与 `label.column`（0-based）两种写法指向同一列 | 两种写法得到同一组行级标签；`column` 写法在表头被改名后仍能工作 |
| 单元：重命名与白名单 | 列声明键=属性名、`header` 指来源列（`"kind": {"header": "type"}`）→ 属性 `kind` 取自 `type` 列；未在 `columns` 声明的列（如仅作 `label.header` 的 `type`）**不产生属性** | 读回顶点：`organisation.kind` 存在且值等于 CSV 的 `type` 列、属性 `type` 不存在；`place` 上不存在 `type` 属性（缺陷存在时会多出该属性）；声明 `"pk": "personId"` + `"columns": {"personId": {"header": "id", "type": "INT64"}}` 时唯一索引建在属性 `personId` 上（列是 `id`） |
| 单元：配置冲突检测 | 两个属性指向同一列 / `pk` 引用未声明的属性 / `pk` 里出现 `header` 或 `type` / `header` 在表头中不存在 | 各自**启动即报错**并指出冲突双方；缺陷存在时会静默取其一（数据错值难以发现） |
| 单元：strict 语义 | 漏声明一列 → 报错**并列出该列名**；显式 `--no-schema-strict` → 回退推断并打 INFO | 同一份残缺配置在两种模式下分别报错 / 成功，能区分 |
| 单元：`undeclared_files` | schema 未声明某文件：默认 `"error"` → 报错；`"ignore"` → 不报错且文件仍被列出 | 同一目录两种配置行为不同 |
| 单元：类型解析 | 每种 `CsvColumnType` 的正常/空值/非法值 | 非法值报错含「文件:行:列:值」 |
| 单元：时序解析 | `DATE`/`DATETIME`/`DATETIME_WITH_TZ`/`TIME`/`DURATION` × {ISO, epoch_ms, 空值, 非法} | 与 `date()/datetime()/duration()` 函数的解析结果**逐字段相等**（复用同一解析器，验证搬家没走样） |
| 单元：批量写入维护索引 | `batchInsertVertices(entries, label_defs)` 后：唯一索引、非唯一索引、`WRITE_ONLY` 索引都产生条目；accessor 缺值的顶点不产生条目 | 装载后直接 `scanVerticesByIndexId` 能查到刚写入的顶点（缺陷存在时查不到 —— 这是本次机制改造的核心判据）；边侧同理覆盖 `batchInsertEdges` |
| 单元：复合主键 | 声明 `pk: ["tenantId", "userId"]`（属性名）：① 元组相同才判重（`(t1,5)` 与 `(t2,5)` 是不同顶点）；② 索引 accessor 顺序 = 声明顺序；③ 端点解析按元组查；④ 非法配置：`pk` 引用未声明属性 / 引用未声明类型的属性 / 重复属性 / 在 `pk` 里写了 `header`（应报错） | ①②③ 全部成立；④ 四种非法配置各自报错并指出具体属性（缺陷存在时会出现「元组顺序错位 → 端点全查不到」，边数骤降可判据） |
| 集成：复合主键端到端 | 构造 `(tenant,id)` 复合键的点文件 + 引用该主键的边文件，装载后查边与点查计划 | 边全部连通（`skipped_unresolved` = 0）；`MATCH (n:User {tenant:$t, id:$i})` 的计划命中复合唯一索引；同一 `id` 不同 `tenant` 的两个顶点都保留 |
| 单元：主键预检的 first-wins | 同标签同主键值写入两条：第一条成功、第二条被跳过并计数 | 顶点数 = 1、索引里 1 条、`duplicate_pk` = 1；索引 `state` 仍为 `PUBLIC`（不是 `ERROR`） |
| 集成：装载 | 现有 [test_loader_integration.cpp](../../../tests/test_loader_integration.cpp#L183-L271) 改造（去掉 `id_maps` 断言）后全量装载 + 边连通性 | 顶点数、边数与 CSV 行数一致；`MATCH (a)-[r]->(b)` 可返回 |
| 集成：主键解析 | **新增**：无 loader 侧映射时 `(:person)-[:knows]->(:person)`、`(:person)-[:isLocatedIn]->(:place)` 仍连通 | 边数必须等于 CSV 行数（147 万量级）；若端点解析有缺陷会显著偏少 —— 该用例在缺陷存在时会失败 |
| 集成：装载规模 | sf0.1 全量装载后核对图结构（**期望值来自 CSV 本身**，不来自 loader 自己的计数） | 顶点总数 = **327,588**、边总数 = **1,477,965**（逐文件 `wc -l - 1` 求和，本机实测）；主键声明后按标签核对各标签计数 |
| 集成：无主键导入 | 只声明部分标签的主键，其余不声明 | 未声明者照常导入成功（口径 1）；边引用它 → `skipped_unresolved`，不报错 |
| 集成：重复主键 | 构造重复 `id` 的 CSV | 顶点数 = 去重后数量 + 告警；边仍能解析到先写入者（first-wins）；`CREATE UNIQUE INDEX` 成功且索引为 `PUBLIC`（预检生效的判据） |
| 集成：行级标签 | `place.csv` 配 `label.header=type`（值 country/city/continent）装载后 | 顶点带 `[place, <列值>]` 两个标签（`MATCH (n:city)` 可命中）；**属性在 `place` 下**（`MATCH (n:place {name:...})` 命中、`MATCH (n:city {name:...})` 也命中同一顶点）；主键索引条目数 = 该文件行数（1460），一条覆盖全部行级标签 |
| 集成：主键即普通索引 | 查询侧验证 | 计划中 `MATCH (n:person {id: $id})` 使用该索引 —— 与旧设计（主键表不接规划器）相比这是新增能力 |
| 集成：类型声明 | sf0.1 + schema JSON 装载后读回类型 | `email` = STRING_ARRAY、`length` = INT64、`birthday` = INT64（按配置而非推断） |
| 回归：TCK | 时序解析器搬家后 | TCK 时序用例全绿，报告与基线一致 |
| 回归：neo4j 对照 | 按 [ldbc-snb-sf0.1-comparison.md](../../benchmark/ldbc-snb-sf0.1-comparison.md) 跑 complex-1..14 | 与本方案前逐行一致（装载语义不得改变查询结果）；标签名按配置实际写入的值核对（§6.4） |

---

## 11. 实施顺序（每步可独立验证）

1. **CSV 读取能力（§3）**：BOM 剥离、RFC 4180 引号解析、分隔符进 schema（`,`,`;`,`\t`,`|` 及任意多字符）、每行列数校验与可操作报错；`--schema` 的文件存在性校验。这一步**独立于主键化**，可单独合并与验证（用构造的 Excel 形态样本）；
2. **时序解析器上提**：`common/types/temporal_value` 公开 `parseXxxString`，`temporal_functions.hpp` 改为调用 → TCK 回归（纯搬家，先做以隔离风险）；
3. **批量写入维护索引（机制改造）**：索引条目收集/校验逻辑从 `query/physical_plan/operator` 下沉到 `storage/data`，`batchInsertVertices` / `batchInsertEdges` 改签名带 `label_defs` 并在同事务写索引条目；query 算子改调新位置 + 全量回归（这一步独立于 loader，可单独验证）；
4. **元数据**：`LabelDef.pk_prop_ids`（**有序**，复合主键即多元素）、`meta_codec` 编解码（直接改格式，不做兼容分支）、`createLabel` 的主键声明与「配套唯一索引按同序覆盖」校验 + 单元测试；
5. **Thrift 协议 + 服务端解析**：`PkKey` / `PkRef`（`keys` 列表天然支持复合键）/ 返回类型变更、`GraphService` 两条路径改为走索引（**按元组**预检 + `scanVerticesByIndexId`）+ `test_rpc_integration`；
6. **Loader 改造（单一入口）**：删除 `--nodes`/`--relationships` 与目录扫描（约 158 行）、`LoadedIdMaps` → `PkNameMap`、顶点/边装载按 `labels`/`relationships` 分组改写（含**复合主键的元组取值与索引 DDL**、行级标签只追加不抢主标签）、`createUniqueIdIndexes` 改为「按 schema 键 + 声明的 pk 列」并**移到点装载之前**（§6.3）+ 集成测试；
7. **JSON schema + 类型配置**：schema 解析并并入 `csv_loader.{hpp,cpp}`（未单独拆 `schema_config.*`）、分隔符与时间格式的**全局默认 + 文件级覆盖**、`undeclared_files`、`--no-schema-strict` 与 strict/推断两种语义、时序类型接入；用交付配置 `social_network-sf0.1-CsvComposite-LongDateFormatter/loader-schema.json` 做 sf0.1 全量装载验收；
8. **文档同步**：本文件的 §6/§7/§8 随实现更新、[loader.md](../usage/loader.md) 补配置说明、[rpc-service.md](../../service/rpc-service.md) 更新批量端点、[data-model.md](../../architecture/data-model.md) 补主键语义；
9. **基准复测**：SF0.1 装载耗时 + 查询结果对照（§9 判据）。

---

## 12. 已知缺陷

> 本节记录**已复现但未修**的缺陷。它们直接影响"跑官方 LDBC 查询"的正确性与性能，
> 在修复前不要据此判定引擎性能。

### 12.1 派生标签上的 `CREATE INDEX` 会写入错误的值（P0，静默漏结果）

**现象**（sf0.1，327,588 顶点）：对行级派生标签建索引后，点查返回**空**：

```cypher
CREATE INDEX idx_msg_id FOR (n:Message) ON (n.id);          -- 报 "Index created"，状态 ONLINE 100%
MATCH (m:Message {id: 893353237791}) RETURN count(m);       -- 0（错）
MATCH (p:Post   {id: 893353237791}) RETURN count(p);        -- 1（同一顶点，正确）
```

**根因证据**（直接 dump 索引表的键，解码后）：

| 索引表 | 条目数 | 键内容 |
|---|---:|---|
| `vidx_1`（`Comment.id`，loader 建的**强** accessor 唯一索引） | 151,043 | `id=9, entity=1`、`id=36, entity=37` … 正常递增 |
| `vidx_3`（`Message.id`，DDL 建的**弱** accessor 索引） | **0** | **空表** |

即**回填的写入根本没有落到索引表里**（不是"写入了错的值"），而索引自报
`ONLINE`、`populationPercent=100.0`，于是规划器照样选中它，**静默漏掉匹配的行**。

**加大 RPC 超时不能解决**（已实测）：把 `rpc_client.cpp` 的 30s 超时临时调到 30 分钟，
`CREATE INDEX FOR (n:Message) ON (n.id)` 耗时 36.8s **完整跑完**、状态 `ONLINE 100%`，
点查**仍然返回 0**，`vidx_3` **仍然为空**。所以"30s 超时截断回填"这个解释是**错的**，
超时只是把问题暴露得更早，不是成因。

**已否证的方向**（不要再往这些方向查）：

- **不是**"多个来源标签给属性分配的 prop_id 不一致"——已用最小图验证（`Comment: id→0`、
  `Post: id→2`，两个顶点都正确进索引、点查命中 2 行）；
- **不是**"弱 accessor 不支持派生标签"——`City` 这类派生标签 + 索引的最小图**正确**；
- **不是**"弱 accessor 取错了属性值"——加诊断打印后实测取值正确
  （`[diag] vid=20385 label=Comment prop='id' (pid=3) -> 618475290625`）；
- **不是** RPC 超时截断（见上：调到 30 分钟仍复现）。

**当前定位**：只在**大标签（28 万顶点）+ 弱 accessor 回填**这个组合上复现，
且表现为**写入未落地**（空表）而非写错值。同一代码路径在 3000 顶点规模
（`index_e2e` 的 `WeakIndexBackfillScalesWithVertexCount`）**回填完整**，
临界点尚未定位——这是留给索引构建重构的具体线索。

**注意**：`Message {id}` 的点查在**没有**该索引时是**正确**的（走 `LabelScan`，返回 1）。
危险恰恰来自"建了索引"：规划器选中空索引后结果变成 0。

**影响**：任何"给派生标签加索引"的做法（包括照抄官方 `indices.cypher`）在修复前都会
让对应该标签的点查**返回错误结果**，而不是变慢。这比缺索引更危险。
**不要**用"加大 RPC 超时"来绕过——已实测无效。

**后续**：索引构建会被重构（参考 pg/MySQL 的回填流程：后台任务 + 分批 + 支持构建期间的
实时写入）。本节作为该重构的输入，记录了现象、已否证方向与判定手段。

**判定手段**（已建立，可复用）：

```bash
# 直接看索引表里到底写了什么键
build/debug/wiredtiger-install/bin/wt -h <db>/graph_0/data -R dump -x table:vidx_<index_id>
# key = 0x02 + 8 字节（int64 按位取反的 big-endian） + 8 字节 entity id
```

### 12.2 loader 不会为派生标签/二级属性建索引，官方 short 查询因此全表扫描

官方 `ldbc_snb_interactive_v1_impls/cypher/scripts/indices.cypher` 要求：

- 10 个标签的 `id` **唯一约束**：`City` / `Comment` / `Country` / `Forum` / `Message` /
  `Organisation` / `Person` / `Post` / `Tag` / `TagClass`（注意含**派生标签** `City`/`Country`/`Message`）；
- 6 个二级索引：`Country(name)`、`Message(creationDate)`、`Person(firstName)`、
  `Post(creationDate)`、`Tag(name)`、`TagClass(name)`。

而 loader 只为 **schema 键（8 个主标签）** 建 `id` 唯一索引，**没有**派生标签索引，
也没有任何二级索引，schema 里也无法声明索引。

**后果**（实测，同库同数据）：`short-4/5/6/7` 查询的 `MATCH (m:Message {id: …})` 走
`LabelScan`（28 万顶点）：

| 条件 | 计划 | 耗时 |
|---|---|---|
| `Message(id)` 无索引 | `LabelScan` | **3144.85 ms** |
| 索引存在且内容正确 | `IndexScan` | **2.39 ms** |

约 **1300×**。文档 [ldbc-snb-sf0.1-comparison.md](../../benchmark/ldbc-snb-sf0.1-comparison.md)
里 short-4/5/6/7「快 34–93×」的读数取自带索引的图（该文档 §2.3.2 第 1 条的日志留有
`Created vertex index 'idx_msg_cd' … on Message.(creationDate)`），**不是**当前 loader 产物。

**待办**：① 修 §12.1；② 让 schema 能声明索引（对齐 `indices.cypher`），loader 建完后
**校验可用性**（`populationPercent` + 一次点查自检），而不是只看 "Index created"。

### 12.3 不支持 neo4j 的 `CREATE CONSTRAINT` 语法

`indices.cypher` 用的是 neo4j 语法：

```cypher
CREATE CONSTRAINT ON (n:Person) ASSERT n.id IS UNIQUE;
```

当前只支持 `CREATE UNIQUE INDEX idx_name FOR (n:Label) ON (n.prop)`；上面那种写法报
`SyntaxError: UnexpectedSyntax`。若要让用户"直接照抄 indices.cypher"，需要补这个语法
（或提供等价的迁移说明）。
