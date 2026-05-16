# TCK 测试失败分类报告

**日期**: 2026-05-16 (更新)  
**分支**: feature/with-match-order-binder (分类6修复 + 多MATCH skip移除)  
**总计**: 3897 场景, 16006 步骤  
**检测方式**: Parser AST 遍历 (替换 regex-based skip)

---

## 总体结果

| 状态 | 场景数 | 占比 |
|------|--------|------|
| 实际运行 (无内部skip) | ~1983 | 50.9% |
| 内部 Skip (AST 检测到不支持语法) | 1914 | 49.1% |
| 运行时错误 (含 TCK 期望的错误匹配) | ~1965 | 50.4% |
| 真正通过 | ~18 | 0.5% |

> **本次更新**: 实现分类6 CrossProduct 修复 + 移除多 MATCH skip。
> 内部 skip: ~2800+ → 1914 (-900+, 多 MATCH 等不再被跳过)。
> 分类6 `MATCH after WITH unrelated variable` 错误: ~24 → **0** (完全消除)。
> 新暴露: WITH 作为首子句 478 个 (之前被多 MATCH skip 掩盖)。

---

## 分类 1: AST 精确 SKIP — 不支持的语法（设计如此，不修复）

这些场景因查询使用了尚未实现的语法而被 AST 遍历器跳过。
跳过是预期的，因为对应的查询引擎能力尚未实现。

### 1.1 不支持的子句类型

| 子句 | 跳过次数 | 优先级 | 说明 |
|------|---------|--------|------|
| MERGE | 63 | P1 | 合并创建，需要条件扫描+创建逻辑 |
| DELETE / DETACH DELETE | 38 | P1 | 删除顶点/边 |
| UNWIND | 16 | P1 | 展开列表为行 |
| REMOVE | 16 | P2 | 删除属性/标签 |
| OPTIONAL MATCH | 8 | P1 | 可选匹配，需要 outer join 语义 |
| CALL | 2 | P3 | 存储过程调用 |
| standalone CALL | 1 | P3 | 顶层 CALL |

### 1.2 不支持的语法模式

| 模式 | 跳过次数 | 优先级 | 说明 |
|------|---------|--------|------|
| 逗号分隔的 CREATE 路径 | 187 | P1 | `CREATE (a), (b), (a)-[:R]->(b)` |
| 多标签节点 | 7 | P2 | `CREATE (:A:B)` |
| 关系类型交替 | 3 | P2 | `[:A|B]` |
| Parse Error | 4 | P3 | Parser 无法解析的语法变体 |
| ~~多个 MATCH 子句~~ | ~~198~~ | ~~P1~~ | ✅ 已支持 (CrossProduct) |

**结论**: 此分类均属于查询引擎功能缺失，不需要修改 TCK 框架。优先在后续版本中实现对应功能。

---

## 分类 2: 缺少步骤定义（Undefined Steps）

80 个场景因缺少 Gherkin 步骤定义而标记为 undefined（另有 82 个场景因 AST skip 更精确而新暴露）。

### 2.1 步骤模式统计

| 缺失步骤模式 | 次数 | 优先级 | 说明 |
|-------------|------|--------|------|
| `the result should be (ignoring element order for lists)` | 21 | P2 | 列表忽略顺序比较 |
| `parameters are:` | ~40 | P2 | 参数化查询 |
| `executing control query:` | ~30 | P2 | 控制查询（setup 的一部分） |
| `there exists a procedure ...` | ~35 | P2 | 存储过程定义 |
| `a TypeError should be raised at any time: ...` | ~15 | P2 | 任意时间抛 TypeError |
| `the binary-tree-1 graph` | ~10 | P3 | 预定义图结构（二叉树） |
| `the binary-tree-2 graph` | ~10 | P3 | 预定义图结构（二叉树变体） |

### 2.2 分析与建议

- **参数支持** (`parameters are:`) 和 **控制查询** (`executing control query:`) 是 TCK 框架的基础能力，建议在 P1/P2 中实现
- **列表忽略顺序比较** 可用于当前 MATCH 结果验证
- **存储过程** 相关步骤是远期目标
- **预定义图结构** 需要根据 TCK feature 文件定义来加载

---

## 分类 3: 绑定错误 — 属性未找到

查询引擎报错 "Property 'X' not found on any label for variable 'Y'"。

### 3.1 根因

TCK 特征文件中的 `CREATE ({prop: val})` 创建无标签节点，之后 `RETURN n.prop` 时 binder 无法找到属性到标签的映射。

**典型查询**:
```cypher
CREATE ({name: 'foo'})
RETURN name  -- 失败: 匿名节点无标签, 绑定器找不到 'name' 属性
```

### 3.2 统计 (TCK 实测 444 次)

| 错误模式 | 次数 |
|----------|------|
| Property 'X' not found on any label for variable 'Y' | 444 |

### 3.3 优先级: P1

这是当前最高频的运行时失败类型（444 个场景）。之前被多 MATCH skip 掩盖，实际影响远超最初估计的 ~20。修复方向：
1. 对无标签节点的属性操作使用动态属性存储
2. 或在 binder 中为无标签节点创建一个隐式的通用 schema

---

## 分类 4: 绑定错误 — 边类型未找到 ✅ 已修复

查询引擎报错 "Edge type 'X' does not exist"。

### 4.1 根因

`ensureTypesForQuery()` 未能从 query 文本中提取到边类型名称，或 CREATE 语句中边类型需要在创建边之前预先注册。

**典型查询**:
```cypher
CREATE ()-[r:R {num: 42}]->()
RETURN r.num  -- 失败: Edge type 'R' does not exist
```

### 4.2 统计

| 错误模式 | 次数 |
|----------|------|
| Edge type 'R' does not exist | ~5 |
| Edge type 'X' does not exist | ~1 |
| Edge type 'FOO' does not exist | ~1 |
| Edge type 'KNOWS' does not exist | ~1 |

### 4.3 修复方案（已实现）

**分支**: `feature/auto-create-edge-type-on-create`

Binder 检测到边类型或属性不存在时，不修改数据库，仅在 `BoundCreateEdgeOp` 中标记 `label_name`（边类型缺失）和 `pending_props`（属性缺失）。PhysicalPlanner 在 planBound 阶段自动插入运行时 DDL 算子：

1. **边类型不存在** → 插入 `CreateEdgeLabelPhysicalOp`：创建元数据 + WT 数据表 + 属性定义
2. **边类型存在但属性缺失** → 插入 `AlterEdgeLabelPhysicalOp`：为已有边类型添加属性列
3. `CreateEdgePhysicalOp` 执行时通过 `pending_props` 按属性名解析 pid（pid 在 DDL 算子执行后才分配）

**状态**: ✅ 已修复，所有 Classification 4 场景现已通过。

---

## 分类 5: 绑定错误 — 函数未找到 ✅ 已修复

查询引擎报错 "Function not found: XXX"。

### 5.1 统计

| 缺失函数 | 次数 | 优先级 | 状态 |
|----------|------|--------|------|
| `type(EDGE)` | ~5 | P1 | ✅ 已实现 |
| `last(LIST<EDGE>)` | 1 | P2 | ✅ 已实现 |
| `shortestPath()` | 已 SKIP | P2 | 待实现 |

### 5.2 修复方案（已实现）

**分支**: `feature/type-last-functions-evalctx`

1. **`type(Edge) -> String`**: 通过 `EvalContext` 将 Catalog 传入函数回调。
   `EdgeValue.label_id` → `Catalog::lookupEdgeLabel()` → `EdgeLabelDef.name`。
2. **`last(List<Any>) -> Any`**: 返回 `ListValue.elements.back()`，空列表返回 null。
3. **`type(Vertex)` 错误分类**: 绑定时检测到参数类型不匹配，报
   `SyntaxError: InvalidArgumentType`（而非泛化的 `Function not found`）。
4. **`EvalContext` 基础设施**: `ScalarFn` / `BatchScalarFn` 签名新增 `const EvalContext&`
   参数，为后续需要访问 schema 的函数提供通用上下文传递机制。
5. **自环修复**: `CREATE (a)-[:T]->(a)` 在 binder 中不再为同一变量创建重复的
   `BoundCreateNodeOp`。

**状态**: ✅ 已修复。TCK 场景 [7] "Failing when using `type()` on a node" 通过。
其余 `type()` 场景的失败根因是前置依赖（分类 3 无标签节点、分类 6 WITH/MATCH 顺序）。

---

## 分类 6: 绑定错误 — WITH/MATCH 子句顺序 ✅ 已修复

查询引擎报错 "WITH without preceding clause" / "Cannot have MATCH without preceding context"。

### 6.1 根因

某些 TCK 测试是多部分查询（Multi-part Query），格式为：
```cypher
MATCH ...
WITH ...
MATCH ...
RETURN ...
```

Binder 在 MATCH 出现在 WITH 之后时，如果 MATCH 引入的新变量不在 WITH 输出作用域内，会错误拒绝。

### 6.2 统计

| 错误模式 | 之前 | 现在 |
|----------|------|------|
| MATCH after WITH on unrelated variable (分类6) | ~24 | **0** |
| Cannot have MATCH without preceding context (级联) | ~24 | 9 (根源是分类3) |

> `WITH without preceding clause` 的 478 次出现已确认为独立问题（分类9），非分类6。

### 6.3 修复方案（已实现）

**分支**: `feature/with-match-order-binder`

新增 `BoundBinaryJoinOp`（逻辑层）+ `CrossProductPhysicalOp`（物理层）算子，支持 MATCH-after-WITH 引入独立变量的笛卡尔积语义。

**架构**:
```
         CrossProduct         ← BoundBinaryJoinOp (JoinType::Cross)
        /            \
  Project(a)     LabelScan(b)  ← 右边可以是任意算子链
```

**修改要点**:
1. 新增 `JoinType` 枚举 + `BoundBinaryJoinOp` 逻辑算子（`join_type.hpp`, `bound_binary_join_op.hpp`）
2. 新增 `CrossProductPhysicalOp` 物理算子（流式嵌套循环）
3. `bindSingleQuery`: 检测 MATCH+WITH 独立模式，自动构建 CrossProduct + 延迟 MATCH WHERE
4. `bindMatch`: 新增 `skip_where` 参数
5. 所有 visit 分支（column_resolver, pushdown, memo, physical_planner）均更新

**物理执行**: 流式嵌套循环，内存 O(left_batch + right_batch + output_buffer)，无暴涨风险。

**Join 扩展性**: `BoundBinaryJoinOp.join_type` 支持 Cross/Inner/Left，后续实现 InnerJoin 只需在物理层替换为 HashJoinPhysicalOp。

**已移除了 TCK 的 `countMatchClauses > 1` skip**。

**状态**: ✅ 已修复。所有 WITH 相关单元测试通过（45/45），完整测试套通过（197/197）。

**暂未支持**:
- WITH 作为首个子句 (`WITH 1 AS x RETURN x`) — 独立问题
- 同个 MATCH 中的混合模式 (`MATCH (x), (a)-->(b)` 在 WITH 之后) — 后续迭代

---

## 分类 7: 错误消息不匹配 — 引擎报错但类型/消息与 TCK 期望不符

TCK 期望特定错误类型（如 SyntaxError / TypeError），但引擎返回的是 RuntimeError 或其他错误消息。

### 7.1 典型情况

| TCK 期望 | 实际返回 | 原因 |
|----------|---------|------|
| SyntaxError: UndefinedVariable | RuntimeError: Property 'name' not found | 错误分类逻辑不够精细 |
| TypeError: InvalidArgumentType | RuntimeError: ... | 参数类型校验未实现 |
| SemanticError: ... | RuntimeError: ... | 缺少语义分析阶段的错误分类 |

### 7.2 优先级: P2

需要完善 `tck_context.cpp` 中的 `executeQuery()` 错误分类逻辑，
以及服务端的错误类型定义。

---

## 分类 8: 其他引擎局限性

| 错误 | 次数 | 优先级 | 说明 |
|------|------|--------|------|
| Named path with mixed fixed/varlen chain | ~2 | P2 | 混合定长/变长路径链 |
| Variable '(a)-[:T*]->(b:MissingLabel)' not defined | ~1 | P2 | 变长路径的标签推导 |

---

## 分类 9: 绑定错误 — WITH 作为首个子句

查询引擎报错 "WITH without preceding clause"。

### 9.1 根因

`bindWith` 检查 `!current` 时拒绝没有前置子句的 WITH：
```cypher
WITH 1 AS x RETURN x
WITH {name: 'baz'} AS nestedMap RETURN nestedMap.name.name2
```

但 openCypher 允许 WITH 作为查询的第一个子句。

### 9.2 统计 (TCK 实测 478 次)

| 错误模式 | 次数 |
|----------|------|
| WITH without preceding clause | 478 |

### 9.3 优先级: P1

影响面大（478 个场景），之前被多 MATCH skip 掩盖。修复方式：在 `bindWith` 中允许 `current == nullopt`，创建纯 Project 算子（无 child）。

---

## 优先级汇总

| 优先级 | 分类 | 影响场景数 | 修复路径 |
|--------|------|---------------|---------|
| **P1** | 分类3: 属性未找到 (无标签节点) | 444 | binder 支持动态属性 |
| **P1** | 分类9: WITH 作为首个子句 | 478 | bindWith 允许空 current |
| **P1** | 分类1: MERGE/DELETE/UNWIND/OPTIONAL | ~2800 | 逐个实现子句 |
| **P2** | 分类2: 缺失步骤定义 | 162 | 补实现步骤 |
| **P2** | 分类7: 错误消息不匹配 | ~50 | 错误分类精细化 |
| **P3** | 分类8: 引擎局限性 | ~3 | 后续迭代 |
| ~~P1~~ | ~~分类4: 边类型未找到~~ | ~~~8~~ | ✅ 已修复 |
| ~~P1~~ | ~~分类5: 函数未找到 (type等)~~ | ~~~6~~ | ✅ 已修复 |
| ~~P1~~ | ~~分类6: WITH/MATCH 顺序~~ | ~~~24~~ | ✅ 已修复 |

---

## 下一步建议

1. ~~**立即**: 合并当前 PR（TCK 基础设施 + graph_manager 竞态修复 + AST 精准 skip）~~ ✅ 已完成
2. ~~**P1-1**: 改进 `ensureTypesForQuery()` 用 AST 提取类型（解决分类4）~~ ✅ 已修复
3. ~~**P1-2**: 实现 `type()` / `last()` 函数 + EvalContext 基础设施（解决分类5）~~ ✅ 已修复
4. ~~**P1-3**: 增强多部分查询支持（解决分类6）~~ ✅ 已修复
5. **P1-4**: 支持无标签节点的属性访问（解决分类3，444 场景）
6. **P1-5**: 支持 WITH 作为首个子句（解决分类9，478 场景）
7. **P2**: 补全 TCK 步骤定义（分类2）
