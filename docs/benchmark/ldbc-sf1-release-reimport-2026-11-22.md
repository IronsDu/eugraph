# SF1 Release 重新导入与 LDBC 性能（2026-11-22）

## 数据与构建

- 分支：`feature/generic-hash-join`（PR #325）；
- 构建：`build/release`；
- 语料：`social_network-sf1-CsvComposite-LongDateFormatter`；
- schema：`docs/program/design/ldbc-sf01-composite.schema.json`；
- graph：`sf1`；
- 数据集：3181724 vertices，17256038 edges；
- 索引：17 个索引全部 `PUBLIC`（`SHOW INDEXES`，progress=100，delta_backlog=0）。

## 导入

统计 sink 开启时，导入在 Comment 约 35% 处卡住并最终 RPC timeout。  
禁用统计 sink 后（`EUGRAPH_STATS_DISABLE=1`）：

```text
Vertices: 3181724 written, 0 skipped
Edges:    17256038 written, 0 skipped
All data loaded into graph 'sf1' in 253.0s
```

## LDBC Interactive（固定 personId=933，warmup=1，iters=3）

```text
query                               rows   min(ms)  median(ms)
interactive-complex-1                 20      53.8        54.4
interactive-complex-10                10     455.5       487.3
interactive-complex-11                10      64.4        64.8
interactive-complex-12                 4     803.7       805.6
interactive-complex-13                 0       1.8         2.0
interactive-complex-14                 0       3.6         3.6
interactive-complex-2                 20     111.2       111.4
interactive-complex-3                  1    1692.8      1713.1
interactive-complex-4                  0      55.5        56.0
interactive-complex-5                 20    2821.6      2822.6
interactive-complex-6                 10    2047.8      2063.3
interactive-complex-7                  5       4.1         4.3
interactive-complex-8                 20       2.4         2.7
interactive-complex-9                 20    3619.6      3621.3
interactive-short-1                    1       1.0         1.1
interactive-short-2                   10       2.7         3.3
interactive-short-3                    5       1.1         1.1
interactive-short-4                    0       0.9         0.9
interactive-short-5                    0       0.7         0.8
interactive-short-6                    0       0.8         0.9
interactive-short-7                    0       1.1         1.1
```

说明：

- 0 行查询使用了脚本默认/不存在的参数，只作为执行路径/延迟样本，不用于结果正确性结论；
- 运行前已确认所有索引 `PUBLIC`；
- 统计 sink 开启时导入卡住是当前已知问题，需要单独定位后才能把 stats 作为默认导入路径。
