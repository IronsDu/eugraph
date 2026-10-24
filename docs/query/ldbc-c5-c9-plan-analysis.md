# LDBC complex-5 / complex-9 计划分析

> complex-14 的优化见 [pattern-comprehension-anchor-label-scan.md](pattern-comprehension-anchor-label-scan.md)。
> 本文记录 complex-5/9 的剩余瓶颈和下一步实现方向；当前代码未对这两条查询做提交。

## complex-9

### 计划（eugraph）

```text
Limit
  Project
    Sort(date DESC, id ASC)
      Filter(message.creationDate < maxDate)
        ProjectionExtract(
          friends, friend, message,
          coalesce(content,imageFile),
          message.creationDate)
          Expand(friend<-HAS_CREATOR-message)
            Unwind friends
              Aggregate(collect(distinct friend))
                VarLenExpand(root KNOWS 1..2)
                  IndexScan(Person)
```

### 分阶段耗时（sf0.1，旧数据）

| 阶段 | eugraph |
|---|---:|
| VLE KNOWS 1..2 | 1.85ms |
| Expand HAS_CREATOR（53,192 条） | 47ms |
| 加 creationDate 过滤（7,706 行） | 142ms |
| 只排序/取 top20 | 249ms |
| 加 friend 姓名 | 252ms |
| 加 coalesce(content,imageFile) | **418ms** |
| 完整 query | 420ms |

Neo4j PROFILE：

```text
VarLengthExpand       174
Expand HAS_CREATOR 53,192
Filter              7,706
Top 20                 20
DB hits            60,898
耗时                ~32ms
```

### 根因

- Expand 和日期过滤不是主要差距；
- `coalesce(content, imageFile)` 在 **7,706 行上全部物化**，约 168ms；
- Sort 前已经完成所有输出属性加载；TopN 本身没有收益；
- 大 ID 批量顺序扫 vprop 表实测更慢（765ms），已回退。

### 需要的改动

真正的 TopN 后延迟物化：

```text
Filter(date)
  -> TopN(date, id)          // 只保留 20 行
  -> ProjectionExtract(content/imageFile/friend name)
  -> Project
```

这需要让 `ProjectionExtract` 只加载当前消费者需要的列，而不是在第一个 Filter/Expand 后
就把所有输出属性一次性物化。属于 demand-driven projection/extraction 的改动，不是存储格式改动。

## complex-5

### 计划（eugraph）

```text
VLE -> HAS_MEMBER -> forum 聚合 8,814
  LEFT JOIN
    论坛侧展开 CONTAINER_OF 89,415 posts
      post -> HAS_CREATOR 89,415
        Filter(friend IN friends) 到 262
```

### 分阶段耗时

| 阶段 | eugraph |
|---|---:|
| VLE + membership + forum/friends | 60ms |
| 加 OPTIONAL post count | **478ms** |
| 完整 query | 514ms |

Neo4j 同计划形状约 98ms，但同样先展开 89k posts，再过滤到 262。

### 根因

- `friend IN friends` 在两次 Expand 之后才过滤；
- 89k post 和 89k creator 边全部物化后才丢弃；
- eugraph 的 per-edge/per-source 成本放大了这个错误顺序。

### 需要的改动

1. **vertex-list semi-join**：把 `friend IN friends` 变成 Expand 的目标集合过滤；
2. 或 **join reorder**：从 174 个 friend 出发，展开 incoming HAS_CREATOR，
   再沿 CONTAINER_OF 到 forum，和 forum 集合做半连接，最后补零；
3. 保留 OPTIONAL 语义（无 post 的 forum 返回 0）。

## 结论

- complex-9：需要 demand-driven 延迟物化；
- complex-5：需要 vertex-list semi-join / join reorder；
- 两者都不需要修改存储格式，但都是算子/规划层改动，不能靠阈值或简单谓词下推解决。


## 2026-10-24 修正：c9 的 Project 位于 Limit 之下

重新检查 c9 物理计划：

```text
Limit
  Project(items=[personId, personFirstName, ..., commentOrPostContent])
    Sort(date DESC, id ASC)
      Filter(date)
        ProjectionExtract(...)
```

`Project` 不是 Limit 的父节点，而是 Limit 的子节点。因此：

- 在 Limit 之后追加 LateMaterialize 不会改变已经执行的 Project；
- 之前“Limit 后 PE”导致 Project 列槽错位，本质原因是 Project 已经在 Limit 之下消费了旧 schema；
- 仅新增算子无法解决，必须先拆分 RETURN Project：
  - 下方 Project：只保留 Sort/Limit 需要的键和 `message`/`friend` VertexRef；
  - 上方 Project：在 Limit 之后计算 content/imageFile/姓名。

也就是说 c9 需要 **Projection split**，不是单独的 LateMaterialize。  
这比之前计划中的算子位置更靠前，是 c9 真正的阻塞点。

## 2026-10-24 实施结果与核心原因

### 结果（sf0.1，同一数据/机器，5 轮 min）

| 版本 | complex-9 |
|---|---:|
| 原路径（`SKIP 0` 绕过改写） | 415.4 ms |
| LIMIT 边界 LM + Top-N Sort | 245–249 ms |
| 再加 Filter 边界 LM | **161.7 ms** |

20 行 × 6 列与原路径逐值一致；`EXPLAIN` 为
`Project → LateMaterialize(20 行) → Limit → Sort → LateMaterialize(7,706 行) → Filter → PE`。

### 核心原因

1. **物化顺序错位（已修）**：RETURN 的 content/imageFile/姓名在 LIMIT 前为 7,706 行物化；
   ORDER BY 的 `message.id` 在 Filter 前为 53,192 行物化。两层 LM 分别把它们移到 LIMIT / Filter 之后。
2. **剩余主要成本是存储访问**：Filter 的 `creationDate` 仍需为 53,192 行做 label 批读 +
   按命中 label 的 per-label vprop 批读（约 98 ms）；Expand 53k 边约 45 ms；Top-N Sort、LM、
   20 行输出已接近 0。
3. **判据**：带 `message.id` 的 ORDER BY 249.4 ms，不带 id 144.6 ms —— 105 ms 是 id 的
   预过滤物化，不是排序；这否证了“Sort 慢”的初始归因。

### 对存储改动的含义

- 旧 `vertex_data` 纯 X 属性合并的结论（complex-2..12 无稳定收益、complex-3 约 -20%）
  来自“宽行、多属性”的旧执行路径；现在 c9 每行只读 1 个属性，页局部性影响变小，
  必须在新计划上重新 A/B，不能直接外推。
- 剩余 98 ms 是 label + per-label vprop 两次点查；compact `label_reverse`、
  “label + hot property（creationDate/id）”合并、或 `Message.creationDate` 索引
  是三个候选方向，均需按本文口径 A/B。
- c5 未实施：当前计划是 `LeftJoin` 右侧 `CorrelatedSource` 对每个 left row 执行一次，
  BatchExpand 的 >2048 行路径不会触发；需要右子计划去相关 / join reorder，
  而不是只加批量 Expand。
