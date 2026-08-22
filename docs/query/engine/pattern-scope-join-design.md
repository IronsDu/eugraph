# Pattern 作用域与 Join 等值约束设计

> 目标：以统一机制修复剩余 TCK 失败中的跨 pattern / OPTIONAL 变量复用问题，
> 同时保持合法性检查、作用域绑定和物理列解析的职责分离。

## 一、根因

1. `bindMatch` 对同一 MATCH 的后续 pattern part 直接构造 `CrossProduct(previous, scan)`。
   - 同名变量没有等值谓词；
   - 右 part 若复用外层 `ctx` 中已有的列号，会引用并不存在于右子计划输入中的列。
2. `bindOptionalMatch` 仅在“起点已绑定”时注入 CorrelatedSource；起点未绑定而后续变量已绑定
   （如 `OPTIONAL MATCH (x)-->(b)`）落入无约束分支。
3. 合法性检查（`VariableTypeConflict` / `VariableAlreadyBound`）依赖绑定时的符号表状态，
   与“如何构造 plan”耦合。隔离作用域后这些检查消失，导致错误场景回归。
4. SlotId / ProjectionExtract 对同名变量按 name 解析：全局 `var_slots` last-write 与
   PEPlan 会把左右两个同名变量重定向到同一个 `__pe` 对象槽。
5. varlen 关系列表绑定、标签顺序、副作用计数各有独立根因（见对应小节）。

## 二、模块职责

| 模块 | 文件 | 职责 |
|------|------|------|
| `PatternScopeAnalyzer` | `src/query/planner/binder/pattern/pattern_scope_analyzer.*` | 对 `MatchPatternGraph` 做静态合法性分析，输出 per-part 复用/冲突信息 |
| `JoinEqualityBuilder` | `src/query/planner/binder/join_equality_builder.*` | 统一构造 `Filter(CrossProduct)` 与 `__eq_left/__eq_right` 等值谓词 |
| `PhysicalCrossRefResolver` | `src/query/physical_plan/cross_ref_resolver.*` | 编译期按 CrossProduct 左右子布局解析 `__eq_*` 列引用 |
| `OptionalMatchCorrelator` | `bindOptionalMatch` 内部使用上述组件 | CorrelatedSource + JoinEqualityBuilder 构造右子计划 |
| `VarlenEdgeListFilter` | `varlen_expand_physical_op` 新增输入过滤 | 按绑定关系列表过滤候选路径 |
| `LabelOrderContext` | 逻辑计划 / 格式化 | 保存 query 标签顺序 |

约束：
- Binder 只生成标准算子树；不新增复合 Join 语义。
- 物理层只解析列，不重新构造谓词。
- DPL 不得按 name 提升 `__eq_*` 内部引用。

## 三、设计

### 3.1 PatternScopeAnalyzer

输入 `MatchPatternGraph`（已有），输出：

```cpp
struct PatternPartScope {
    std::set<std::string> new_variables;   // 本 part 新绑定
    std::set<std::string> reused_variables; // 与外层同类型复用（可等值）
    std::vector<PatternConflict> conflicts; // 必须报错的复用
};
struct PatternConflict {
    std::string variable;
    std::string outer_usage; // node / relationship / path / scalar
    std::string inner_usage;
    bool error_is_variable_already_bound;
};
```

规则：
- 同一 part 内：一个变量先作为 node 再作为 relationship/path → conflict；
- 跨 part：同类型（node↔node、relationship↔relationship）→ reused；不同类型 → conflict；
- 外层是标量，pattern 中作为关系 → `VariableTypeConflict`；作为路径 → `VariableAlreadyBound`。

`bindMatch` 在绑定前调用 analyzer；存在 conflict 时先报错，不再绑定。
之后每个 part 在独立 `BindContext` 中绑定，复用变量由 JoinEqualityBuilder 连接。

### 3.2 JoinEqualityBuilder

统一入口：

```cpp
std::optional<BoundLogicalOperator> buildCrossJoinWithEqualities(
    BoundLogicalOperator left, BoundLogicalOperator right,
    const BindContext::Snapshot& left_scope, const BindContext::Snapshot& right_scope);
```

- 对同名变量按类型分类（node/edge/path/scalar）；
- 类型冲突报对应错误码；
- 谓词：
  - 图实体：`id(__eq_left) = id(__eq_right)`
  - 标量：`__eq_left = __eq_right`
- `__eq_left` 使用左 scope 列，`__eq_right` 使用右 scope 局部列；
- 返回值始终是 `Filter(CrossProduct)` 或纯 `CrossProduct`。

### 3.3 PhysicalCrossRefResolver

- `BoundColumnRef.name` 以 `__eq_` 为前缀时：
  - DPL rewrite 不做 PE 提升；
  - ExpressionCompiler 不做 slot 查找；
  - FilterPhysicalOp 编译时按左右子布局解析：
    - `__eq_left` → `left_layout.getColumnIndex(slot)`；
    - `__eq_right` → `right_layout.getColumnIndex(slot) + left_column_count`。

### 3.4 bindMatch 多 pattern part

```
analyzer = PatternScopeAnalyzer(match, ctx)
if analyzer.hasConflict() -> error
for pi in 0..n-1:
    isolate part scope
    bind part with null parent
    previous = buildCrossJoinWithEqualities(previous, current, prev_scope, part_scope)
```

`pi == 0` 仍走既有 parent/needs_cross 逻辑。

### 3.5 OptionalMatchCorrelator

对 `bound_vars` 非空：
1. `ctx.beginSubScope()`；
2. 创建 `BoundCorrelatedSourceOp`（所有 bound vars，局部列 0..k-1）；
3. 在独立 scope 绑定 pattern（`skip_where=true`）；
4. `buildCrossJoinWithEqualities(source, pattern, source_scope, pattern_scope)`；
5. 恢复 source scope 后合并 pattern 新变量，绑定 WHERE；
6. 恢复外层后合并新变量，创建 `BoundLeftJoinOp`（correlation 保持 SlotId 语义）。

### 3.6 varlen 关系列表绑定

`BoundVarLenExpandOp` 新增可选输入：

```cpp
std::optional<BoundExpression> bound_edge_list_expr; // 绑定关系列表
std::string bound_edge_list_var;
```

Binder 在 pattern 的 relationship 为变量且外层已绑定 list 时设置；
VarLenExpandPhysicalOp 在枚举到 depth 后检查该路径的边 ID 集合与列表 ID 集合一致再输出。
列表元素可为 EdgeKey / EdgeValue / Map（取 id）。

### 3.7 LabelOrderContext

- Binder 在绑定带标签节点时记录变量名 → 有序标签名列表；
- ProjectionExtract 构造 VertexValue 时按该顺序放置 labels；
- 仅影响展示顺序，不改变存储编码。

## 四、实施顺序与回归门

1. JoinEqualityBuilder + PhysicalCrossRefResolver 抽取 → `With1/With6/Match2/Match6` 0 失败；
2. PatternScopeAnalyzer + bindMatch 多 part → `Match2/Match6/Match3` 回归；
3. OptionalMatchCorrelator → `Match7/MatchWhere6/With1` 回归；
4. VarlenEdgeListFilter → `Match4/Match9`；
5. LabelOrderContext → `Match3[7]`；
6. CREATE/UNWIND 副作用计数；
7. 全量 TCK + 报告。

## 五、不做的事

- 不修改 `BoundBinaryJoinOp` 数据结构；
- 不引入新的运行时 Join 算子；
- 不在物理执行热路径按变量名字符串比较；
- 不改变存储层标签编码。
