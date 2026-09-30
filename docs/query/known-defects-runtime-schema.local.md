# 已知缺陷（本地草稿）：运行期新建 schema 的同语句可见性

> ⚠️ **本文件是本地草稿，未入库、不提交、不推送**（`git status` 里是 untracked）。
> 内容来自 FOREACH 实现期间的语义对照：本机 neo4j 5（bolt 7687，容器 `eugraph-oracle`）
> 与 eugraph（bolt 7688）并排跑同一批查询。
>
> 要正式留下时：把下面的「缺陷 A」「缺陷 B」并入
> [known-defects-todo.md](known-defects-todo.md) 作为新的编号小节，然后删掉本文件。
>
> 一句话结论：**运行期新建的 label / property，在同一语句内对 `labels()` / `keys()` 不可见**
> —— 与 FOREACH 无关，裸 `CREATE` 就能复现；另有第二类"派生列写后不刷新"（`UNWIND` 同样复现）。
> 两类都是**静默少结果**（不报错），且现有测试与 TCK 都覆盖不到，所以优先级建议不低。

---

## 缺陷 A：运行期新建的 schema 对同语句的 labels() / keys() 不可见

### 现象（每个用例都清库重建，neo4j 作对照）

| 查询（同一条语句内读回） | neo4j | eugraph |
|---|---|---|
| `CREATE (n:Brand {x: 1}) RETURN labels(n)` | `['Brand']` | **`[]`** |
| `CREATE (n:Widget {fresh: 1}) RETURN keys(n)` | `['fresh']` | **`[]`** |
| `MATCH (p:P) SET p:Q RETURN labels(p)`（Q 本次新建） | `['P','Q']` | **`['P']`** |
| `MATCH (p:P) SET p.newProp = 1 RETURN keys(p)` | `['newProp','id']` | **`['id']`** |
| 同上，但标签/属性**已存在**于 catalog | ✅ | ✅ 一致 |
| 同样内容**换一条语句**读 | ✅ | ✅ 一致 |

即：少的**正好是"本语句运行期新建的那个"** schema 项；已存在的、以及换语句后的读都正确。

### 根因

1. `EvalContext.catalog` 是 **`const catalog::Catalog*`**（`src/query/function/function_def.hpp:34`），
   在 prepare 阶段由 meta store **一次性**构建：
   `src/query/executor/query_executor.cpp:113-114`（`catalog->load(...)`）→ `:134`（存进 ctx）→
   `:187`（`plan_ctx.eval_ctx.catalog = ctx->catalog.get()`）。
2. `labels()` / `keys()` 靠 `ctx.catalog->lookupLabel(lid)` 把 id 翻成名字，**查不到就静默跳过**：
   - `labelsImpl`：`src/query/function/scalar/graph_functions.hpp:14`（`if (def) lv.elements.push_back(...)`）
   - `keysVertexImpl`：同文件 `:61`（`if (!label_def) continue;`，属性列表也来自同一份 catalog）
3. 运行期隐式 DDL 只写 meta store（和算子自己的 `label_defs_`），**从不回写 catalog**：
   - `CreateNodePhysicalOp::createOrGetLabel_`：`create_node_physical_op.cpp:104-126`
     （Phase 0 建标签、Phase 0b 补属性）
   - `SetPhysicalOp`：`SET n:NewLabel` → `set_physical_op.cpp:278-280`；
     `SET n.p = ...` 落到 `__anon__` → `:458`、`:682`；给已有 label 补属性 → `:439`、`:659`
   - 边侧对应 `CreateEdgeLabelPhysicalOp` / `AlterEdgeLabelPhysicalOp`
4. `Catalog` 只有 `load()`（`src/query/catalog/catalog.hpp:23`）与统计 setter，
   **没有运行期注册接口**。
5. **catalog 是每语句一份**（`QueryExecutor` 的 `ctx->catalog`），不是跨查询共享
   → 可以放心在语句执行期间改它，不存在并发写风险；现在纯粹是漏了同步。

### 影响

凡是"同语句内建 schema 再按名字读回"的写法都会**静默少结果**：`CREATE (n:X) RETURN labels(n)`、
`SET n:Q RETURN labels(n)`、`SET n.p = 1 RETURN keys(n)`，以及依赖它们的投影/断言。
换一条语句就正常，非常容易被误判成"偶发"或"异步登记"。

补充：**现有测试与 TCK 都覆盖不到**。上游 TCK 里 `labels(` 只出现在 `Call1.feature`
（CALL 被 harness 跳过），没有任何"CREATE/SET 之后同语句读 `labels()`"的场景 —— 所以 CI 全绿
并不代表这里没问题。

### 修法建议

1. `Catalog` 增加运行期注册接口，例如
   `void addOrUpdateLabel(const LabelDef&)` / `void addOrUpdateEdgeLabel(const EdgeLabelDef&)`
   （或 `addLabelProperty(LabelId, const PropertyDef&)`），维护 name↔id 映射与属性列表。
2. 上面列出的隐式 DDL 调用点，在拿到新 `LabelDef` / `PropertyDef` 后同步进 catalog；
   `EvalContext.catalog` 去掉 `const`（或给算子单独一个非 const 的 `Catalog*`，与
   `PhysicalOperator::setEvalContext` 同一条路）。
3. 顺带：算子的 `label_defs_` 与 catalog 现在是两份数据靠手工同步维护，是同类 bug 的温床，
   可考虑收敛成一份（改动更大，可单独排期）。

### 验证计划（先证明用例在缺陷存在时会失败）

- `CREATE (n:Brand {x: 1}) RETURN labels(n)` → 期望 `['Brand']`
- `CREATE (n:Widget {fresh: 1}) RETURN keys(n)` → 期望 `['fresh']`
- `MATCH (p:P) SET p:Q RETURN labels(p)`（Q 新建）→ 期望含 `'Q'`
- `MATCH (p:P) SET p.newProp = 1 RETURN keys(p)` → 期望含 `'newProp'`
- 反面用例（必须继续通过）：标签/属性**已存在**时，同语句读回本来就对
- 回归：全量 `ctest` + TCK（别指望 TCK 兜住，理由见上）

---

## 缺陷 B：写"路径/列表元素"后，同语句读原变量的派生列仍是旧值

### 现象

| 查询 | neo4j | eugraph |
|---|---|---|
| `MATCH p=(a:A {id:1})-[:R]->(b:A {id:2}) FOREACH (n IN nodes(p) \| SET n.marked = true) RETURN [x IN nodes(p) \| x.marked]` | `[true, true]` | **`[null, null]`** |
| 同样逻辑改用 `UNWIND nodes(p) AS n SET n.marked = true`（对照） | `[true, true]` | **`[null, null]`** |
| `MATCH p=... FOREACH (n IN nodes(p) \| SET n.marked=true) RETURN a.marked` | `true` | **`null`** |
| `MATCH p=... UNWIND nodes(p) AS n SET n.marked=true RETURN a.marked`（对照） | `true` | **`null`** |
| `MATCH (a:A {id:1}) FOREACH (n IN [a] \| SET n.marked = true) RETURN a.marked` | `true` | ✅ `true` |

最后一行说明：**被写实体本身是外层变量**时已经正确（FOREACH 的写回覆盖了这种情形）；
错的是"通过路径/列表元素拿到实体再写，然后读原变量的派生列"。

### 根因

读是**提前物化**的：`x.marked` 由 `ProjectionExtract` 的 `LoadVertexProp`
（`projection_extract_physical_op.cpp:369` / `:459` 一带）或
`PathElementPropertyReadPhysicalOp` 的行前物化列提供；而写算子只刷新**实体列**：
- `SetPhysicalOp` 的 `mirrorVertexToAllReferences` 只按实体 id 匹配 **`VERTEX` 列**
  （`src/query/physical_plan/operator/mutation_mirror.hpp:43`）
- `ForeachPhysicalOp::refreshOuterEntities` 同构，同样只覆盖 `VERTEX` / `EDGE` 列
  （`src/query/physical_plan/operator/foreach_physical_op.cpp`）

派生列（属性列、标签列表列、路径元素列）没有任何人刷新；neo4j 那边读的是**事务当前状态**，
所以不会出现这种陈旧读。

### 为什么难

需要先有一张"哪些派生列引用了哪个实体"的登记（PE 的 spec、路径元素列都要登记），
写算子再据此重算；而且它**同时影响 UNWIND**（上表第二行），不是 FOREACH 的局部问题。
属于读模型层面的设计问题。建议：短期只记入 known-gaps、不排期；
真要动的话，先从"同语句读属性"这一条（`LoadVertexProp`）做起，路径元素列留到最后。

---

## 附：FOREACH 实现期间的探针结论（供参考）

- **已对齐**（neo4j 逐值一致）：body SET 后同语句读属性、别名变量（`WITH p AS a, p AS b` 写 a 读 b）、
  `DETACH DELETE` 后同语句读（两边都正确报 `EntityNotFound`）、嵌套 FOREACH 的内层写外层读、
  `count` / 聚合 / `properties()` / `id()` 等。
- FOREACH 的写回覆盖了"被写实体就是外层变量"的情形；**缺陷 A / B 都不是 FOREACH 引入的**
  （裸 `CREATE`、裸 `SET`、`UNWIND` 均可复现），FOREACH 只是让它们更容易被撞上。
- 当时用于对照的一次性脚本放在 `/tmp`（`foreach_compare.py`、`stale_probe*.py`、`label_probe*.py`），
  重启即失；关键查询已内联到上面的表格里，重建脚本时照抄即可。
- 起 oracle 的命令：
  `docker run -d --name eugraph-oracle -p 7687:7687 -e NEO4J_AUTH=neo4j/oracle123 neo4j:5-community`
