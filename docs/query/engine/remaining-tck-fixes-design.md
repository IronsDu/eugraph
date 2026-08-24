# 剩余 TCK 失败修复设计（v5 后收尾）

> 状态：待实现。
> 基线：全量 TCK 3822 通过 / 4 失败（在完成 Match8[2] 与 Create3[3] 修复后）。
> 本文只覆盖仍失败的三个场景：`Match5[27]`、`MatchWhere4[2]`、`WithWhere4[2]`。

## 1. 范围与原则

- 所有修复必须落在既有统一机制上：
  - pattern 绑定与作用域：`pattern-join-planner-design.md`；
  - 表达式绑定/求值：`query-engine-design.md`、`slot-id-design.md`；
  - varlen 执行：`varlen_expand_physical_op.*`。
- 禁止为单个 TCK 场景添加特判。
- 每个修复必须先通过“根因证据 + 计划差异”验证，再改代码。

## 2. Match5[27]：两段 MATCH 的 path 关系唯一性

### 2.1 根因证据

- 场景期望 `MATCH (a:A) MATCH (a)-[:LIKES]->()<-[:LIKES*3]->(c)` 返回 16 行；
- 当前实现返回 20 行；
- 同一图形下，单段 `MATCH p=(a:A)-[:LIKES]->()<-[:LIKES*3]->(c)` 返回 **16 行（正确）**；
- 说明差异来自两种绑定路径：
  - 单段 path-MATCH：varlen/PathBuild 统一持有 path；
  - 两段 MATCH：`Expand`（固定边）+ `VarLenExpand` 是两个独立物理算子，varlen 的 `visited_edges` 不包含前面固定 Expand 已遍历的关系。
- 因此 varlen DFS 可以重新使用前面的固定关系，产生额外路径。

### 2.2 修复设计

1. 在 `bindMatch` 链式绑定中维护 `prev_edge_var`：
   - 固定 Expand 即使关系变量匿名，也已有内部 `__anon_edge_*` 列（`bindRelationshipPattern` 保证）；
   - 当下一 hop 是 varlen 时，把该列名写入 `BoundVarLenExpandOp`。
2. `BoundVarLenExpandOp` 增加：
   - `std::string prev_edge_var;`
   - `uint32_t prev_edge_col_index = 0;`
3. `VarLenExpandPhysicalOp` 构造参数增加 `prev_edge_col_idx`：
   - 每行执行前从输入列读取 `prev_edge_id`；
   - 将 `EdgeVisitKey{prev_edge_id}` 预先加入 `visited_edges`；
   - 初始 `scanAll(src)` 结果中过滤掉 `edge_id == prev_edge_id` 的候选。
4. **验收前必须补诊断**（当前实验未生效，不能直接认为字段接错）：
   - 打印 `prev_edge_var`、`prev_edge_col_index`、读取到的 `prev_edge_id`；
   - 打印被过滤掉的候选边数量；
   - 若 `prev_edge_id == INVALID_EDGE_ID`，检查固定 Expand 输出列在 PE 之后的类型与槽位。
5. 若过滤后仍为 20 行，则进一步记录所有 20 条路径的边 ID 序列，与正确 16 条比对，
   确认额外 4 条是否真的复用了前置边，还是其他关系唯一性缺口。

### 2.3 回归范围

- `match/Match5.feature`（全量）；
- `match/Match4.feature`、`match/Match7.feature`、`match/Match9.feature`；
- `query_executor_tests`。

## 3. MatchWhere4[2] / WithWhere4[2]：混合 OR 中的裸 pattern predicate

### 3.1 根因证据

- 失败查询形态：`scalar AND (pattern1) OR (pattern2)`；
- `bindWhere` 现只支持：
  - 顶层 AND 链中的 `EXISTS`；
  - **全部**由 `EXISTS` 组成的 OR。
- 混合 OR 落入普通 `bindExpression`，`ExistsExpr` 分支报 `UnexpectedSyntax`。

### 3.2 修复设计

目标：让裸 pattern predicate 变成**可参与普通布尔表达式的标量值**，不改变现有
all-EXISTS OR 的 SemiJoin 路径。

1. **AST 语义等价转换**
   - 在 `bindWhere` 中收集谓词中的全部 `ExistsExpr`；
   - 每个 `ExistsExpr` 转换为等价的 `size(patternComprehension) > 0`：
     - `PatternComprehension.patterns = ExistsExpr.patterns`
     - `PatternComprehension.where_pred = ExistsExpr.where_pred`
     - projection 使用 `Literal(1)`（`bindPatternComprehension` 已支持）。
2. **提升（hoisting）**
   - 复用 `bindPatternComprehension` 将 PC 提升为 `BoundPatternComprehensionApplyOp`；
   - 在 Binder 中维护 `exists_as_list_` 映射：
     `ExistsExpr* → (SlotId, output_name, List<element_type>)`。
3. **绑定**
   - `bindExpression` 遇到已映射的 `ExistsExpr` 时，生成：
     `GT(FunctionCall(size, BoundColumnRef(list)), Literal(0))`。
4. **左槽回退**
   - `PatternComprehensionApply` 的 physical planner 在 `left_slot` 找不到时，
     按 correlation 顺序回退到左 schema 列（已验证可消除 slot 错误）。
5. **必须解决 runtime `UnexpectedSyntax`**
   - 当前实验在物理阶段抛 `UnexpectedSyntax`；
   - 下一步先在 `ExpressionCompiler` 打印 `BoundExpression` 节点类型与
     `size`/`GT` 的函数注册结果，定位缺失节点；
   - 候选修复点：
     - `size(List<Int64>)` 未注册时，为 list element 类型增加通用重载；
     - `BoundPatternComprehension` 占位符未 patch；
     - evaluator 对 `BoundFunctionCall(size)` 的 batch fn 不支持。
6. **兼容性**
   - 保留现有 all-EXISTS OR → SemiJoin 分支不变；
   - 仅对“存在非 EXISTS 叶子”的 OR 使用新路径。

### 3.3 回归范围

- `match-where/MatchWhere4.feature`、`with-where/WithWhere4.feature`；
- `match/Match7.feature`（pattern predicate 相关场景）；
- `with/With1.feature`、`with/With6.feature`；
- `query_executor_tests`。

## 4. 文档与状态

- 本文件与 `pattern-join-planner-design.md` 配套；
- 两个修复完成后，更新 `tck-results.md`，并在此文档追加最终差异说明。
