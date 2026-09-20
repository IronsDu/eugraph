# 已知缺陷待办

> 排查过程中**顺带确认**、但与当前任务**无关**的既有缺陷。每条都附最小复现或代码位置，可直接开工。
>
> **来源**：
> * §1–§7：complex-10 正确性缺陷排查（复盘见
>   [comprehension-defect-debugging-notes.md](comprehension-defect-debugging-notes.md)）；
> * §8–§12：complex-5 性能排查（根因与三轮尝试的证据链见
>   [../benchmark/ldbc-snb-sf0.1-comparison.md](../benchmark/ldbc-snb-sf0.1-comparison.md)）。
>
> 其中 §8 是**架构层面的缺口**（相关子计划内的连接不受代价模型影响），建议独立立项；
> §9、§10 是可直接开工的具体修复。

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

## 7. 客户端长连接会被断开

**现象**：单个 bolt 连接连续跑 30+ 次 complex-10（每次 0.4–3s）后，
客户端报 `Failed to read from defunct connection`；**服务器未崩溃**（仍在监听、日志正常、内存充足）。

**影响**：基准脚本需每轮新建连接，否则会把「连接断开」误判为「服务器崩溃 / 查询超时」。

## 8. 相关子计划内的连接不受代价模型影响（complex-5 不返回的根因）

**现象**：LDBC complex-5 永不返回（CPU 121%、内存仅 1.1 GB，非内存问题）。
`OPTIONAL MATCH (friend)<-[:HAS_CREATOR]-(post)<-[:CONTAINER_OF]-(forum) WHERE friend IN friends`
被规划为 `CorrelatedSource → CrossProduct → Filter`，其中 `friend` 无约束 →
**扫描全部节点再与外层的每个 forum 做笛卡尔积**，`friend IN friends` 只在最后当过滤器。

**本质**：`bindCrossWithEqualities` 有等值却产出 `Cross + Filter`，而该子树位于**相关子计划内** ——
优化器**没有可选的替代计划**，代价估计改得再准也**无从发挥**（已实测：把 `join_type`
从 `Cross` 改成 `Inner`，`EXPLAIN` 计划**完全不变**）。

**已验证不可行的三类局部方案**：
1. 把 `Cross + Filter` 改写成 `HashJoin`（实现在 `physical_planner.cpp` 的 `BoundFilterOp` 钩子点，
   与既有 `tryPlanListIndexJoin` 并列）→ **三次尝试均 SIGSEGV**（见第 9、10 项）；
2. 修正 `join_type` 的代价模型输入 → **计划不变**；
3. 在模式中改从已绑定节点起遍历（`bind_match.cpp:1991` 的 `first_node_bound` 只覆盖「首节点已绑定」）
   → **需 AST 层重排模式，未尝试**。

**结论**：这是**架构层面的缺口** —— 需要让相关子计划内的连接进入优化器的选择范围，
或为该形态提供专门的规划路径。**建议独立立项**，不要再做局部试探。

**排查记录**：`docs/benchmark/ldbc-snb-sf0.1-comparison.md`（含三轮尝试的完整证据链）。

## 9. 列索引重映射遍历缺 37 处空值守卫

**位置**：`physical_planner.cpp` 的 `remapExprColumnIndices`（22 处）与 `remapChildOps`（15 处）。

**现象**：这些函数遍历逻辑计划树、解引用每个 `unique_ptr` 节点，但**部分分支有 `if (val)` 守卫、
部分没有** —— 同一函数内不一致。一旦某个节点的 `unique_ptr` 为空（例如谓词内容被 `std::move`
取走后留下的空洞），就会**解引用空指针 → SIGSEGV**。

**证据**：complex-5 的改写尝试中，gdb 抓到
`remapExprColumnIndices (physical_planner.cpp:420)` 解引用空的 `unique_ptr<BoundFunctionCall>`；
补齐全部 37 处守卫后该崩溃消失（查询推进到运行期才崩）。

**建议**：统一补齐守卫（**安全**：空节点跳过比崩溃好），并**顺带核查**
「为何计划树中会出现空节点」——若正常路径也会产生空洞，那是更根本的问题。

## 10. `WITH collect(...)` 无分组键时，列表在后续 OPTIONAL MATCH 的 WHERE 中不可见

```cypher
MATCH (p:Person {id:933})-[:KNOWS*1..2]-(f:Person) WHERE NOT p=f
WITH collect(DISTINCT f) AS friends
OPTIONAL MATCH (x)<-[:HAS_CREATOR]-(post:Post)
WHERE x IN friends                    -- → Binding failed; UndefinedVariable: 'friends' not defined
RETURN count(post)
```

**现象**：`friends` 由上一条 `WITH` 定义，理应可见，却报 `UndefinedVariable`。

**对比**：complex-5 的 `WITH forum, collect(friend) AS friends`（**带分组键**）**不报此错**。

**推测**：疑似「无分组键的聚合结果列在后续子句中的可见性」问题。**未定位**。

## 11. 测量陷阱：固定顺序的 A/B 会让后跑者偏快约 7%

**现象**：同一二进制交错 A/B 时，若**固定顺序**（总是 A 先 B 后），
后跑版本的**基线查询**（该改动不可能影响）也快了约 7%（344/355/334 vs 377/380/356 ms）。

**教训**：**每轮交换顺序**（A→B、B→A、A→B），并用**相对同轮基线的比值**判读，
而非绝对耗时。本次实测中，固定顺序曾让我把「无变化」误读为「改进」。

## 12. `CorrelatedSource` 运行时按值推断列类型（规划类型可能与运行时不符）

**位置**：`correlated_source_physical_op.cpp` 的 `kindFromValue()`。

**代码注释原文**：*"The binder types the correlated variable as VERTEX (semantic), but
LeftJoin passes whatever form the left column actually holds at runtime — typically VertexRef
from a topology-stage Scan."*

**含义**：该算子的输出列类型**由运行时的值决定**，而非规划时的 `types_`。
这解释了为何 `HashJoinPhysicalOp` 需要在**比较**与**哈希**两侧都按 id 归一
（见已修缺陷 `b96ff738`：`KeyHash` 与 `KeyEq` 对边不一致）。

**风险提示**：任何在规划期推断「某列必为 VERTEX」的代码都可能与运行时不符。
