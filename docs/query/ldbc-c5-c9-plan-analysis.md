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

### 已实现的 c9 改动：LIMIT 边界延迟 coalesce

在 `BoundLimitOp` 规划时：

1. 扫描 LIMIT 子树的 Filter/Sort 表达式，收集它们实际使用的 coalesce slot；
2. 其余 coalesce slot（例如只出现在最终 RETURN 的 content/imageFile）标记为 deferred；
3. 规划 LIMIT 子树时由 `dispatchProjectionExtract` 跳过 deferred slot；
4. 构造 `LimitPhysicalOp` 后，再对 LIMIT 的输出执行一次 PE，只对保留下来的行加载这些 coalesce 属性。

实测：

| 版本 | complex-9 |
|---|---:|
| 原实现 | ~420ms |
| LIMIT 边界延迟 coalesce | **368ms** |

结果仍正确（20 行，date 过滤 7,706 行）。提升有限，说明 content/imageFile 仍有一部分在 LIMIT 前被其他需求触发；
但这验证了“延后 coalesce”方向，后续需要独立 LateMaterialize 算子才能完全隔离。

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
