# Cascades 逻辑优化器

> 相关模块：Optimizer、Binder、PhysicalPlanner、QueryExecutor
> 源码：`src/query/optimizer/`

---

## 一、背景与动机

查询执行管线为 `Binder → LogicalOptimizer → PhysicalPlanner`，优化器插入位置在 `query_executor.cpp` 的 `binder.bind()` 之后、`physical_planner.planBound()` 之前。

引入优化器解决的问题：

1. **Filter 位置不合理**：`MATCH (n:person)-[]->(m) WHERE n.firstName = 'Mahinda' RETURN p` 生成的计划为 `LabelScan → Expand → PathBuild → Filter`，谓词只依赖 LabelScan 产出的列，应在 Expand 之前过滤
2. **缺少可扩展的优化框架**：之前的 `applyProjectionPushdown` 硬编码在 Binder 中，无法系统化地添加新规则

### 为何选择 Cascades 框架

图查询的模式匹配优化本质是搜索空间枚举问题——同一语义的查询可能对应多种逻辑计划（不同展开起点、不同 Filter 位置、不同 Join 顺序）。Cascades 框架适合此场景：

- **Memo** 以 Group 为单位组织等价计划，天然支持搜索空间的紧凑存储和去重
- **Task 驱动**将优化过程分解为可调度、可剪枝的细粒度任务，为代价敏感的搜索策略留出空间
- **Rule 接口**将"匹配什么"和"如何变换"解耦，新增规则无需修改框架代码

### 参考来源

本设计参考 [Columbia Optimizer](https://github.com/yongwen/columbia)（Portland State University / Oregon Graduate Institute）的核心架构，使用现代 C++（`std::unique_ptr`、`std::variant`、`std::vector`、`std::deque`）重新实现，不采用 Columbia 的 MFC 风格 C++。

| Columbia 组件 | 本项目对应 | 参考价值 |
|---|---|---|
| SSP | `Memo` | Memo 以 Group + GroupExpr 组织等价表达式 |
| M_EXPR | `GroupExpr` | 算子数据与子节点分离，用 GroupId 引用子节点 |
| RULE + BINDERY | `OptRule` | 四段式规则接口：topMatch → condition → substitute |
| PTASKS | `TaskQueue` + 3 种 Task | Task 驱动的优化循环 |
| LEAF_OP | `PatternNode`（空 children） | Pattern 中的子树通配符 |

---

## 二、整体管线

```
Binder → LogicalOptimizer (Cascades) → PhysicalPlanner
```

```
                               ┌──────────────────┐
                               │  LogicalOptimizer │
                               └────────┬─────────┘
                                        │
                ┌───────────────────────┼───────────────────────┐
                │                       │                       │
         ┌──────▼──────┐        ┌──────▼──────┐        ┌──────▼──────┐
         │   copyIn    │        │   optimize  │        │   copyOut  │
         │ (树 → Memo) │        │ (Task驱动)  │        │ (Memo → 树)│
         └─────────────┘        └─────────────┘        └─────────────┘
```

集成方式（`query_executor.cpp`）：

```cpp
// 2.5. Logical optimization
optimizer::LogicalOptimizer logical_optimizer;
logical_optimizer.optimize(bound_stmt->plan);
```

---

## 三、核心数据结构

### 3.1 Memo（搜索空间）

```cpp
// memo.hpp
using GroupId = uint32_t;
using ExprId = uint32_t;

class Memo {
public:
    GroupId copyIn(BoundLogicalOperator& op);     // 树 → Memo
    BoundLogicalOperator copyOut(GroupId root_gid); // Memo → 树

    GroupExpr* findDuplicate(const GroupExpr& expr);
    GroupId mergeGroups(GroupId g1, GroupId g2);
    GroupExpr* insertExpr(unique_ptr<GroupExpr> expr, GroupId target_group);
    GroupExpr* createGroupWithExpr(BoundLogicalOperator op, vector<GroupId> child_groups);

    Group& getGroup(GroupId id);
    GroupExpr& getExpr(ExprId id);

private:
    vector<unique_ptr<Group>> groups_;
    vector<unique_ptr<GroupExpr>> exprs_;
};
```

- `copyIn`：递归地将逻辑算子树自底向上插入 Memo，每个算子对应一个 Group + GroupExpr
- `copyOut`：从 Memo 中提取优化后的计划。当前为 RBO 策略，取每个 Group 中最后加入的逻辑表达式
- `findDuplicate`：线性搜索同 Group 内类型相同、子 Group 相同的表达式，用于去重
- `mergeGroups`：将两个等价 Group 合并（大 ID 合入小 ID）

### 3.2 Group（等价表达式集合）

```cpp
class Group {
public:
    explicit Group(GroupId id);
    GroupId id;
    vector<ExprId> logical_exprs;
    vector<ExprId> physical_exprs;  // CBO 阶段使用
    bool explored = false;
    bool optimized = false;
};
```

### 3.3 GroupExpr（算子数据 + 子 Group 引用）

```cpp
class GroupExpr {
public:
    GroupExpr(ExprId id, GroupId group_id, BoundLogicalOperator op, vector<GroupId> child_groups);

    ExprId id;
    GroupId group_id;
    BoundLogicalOperator op;       // child 字段在 Memo 中闲置
    vector<GroupId> child_groups;  // 子节点通过 GroupId 引用
    uint32_t rule_mask = 0;        // 已应用规则位掩码

    void markRuleFired(int rule_idx);
    bool canFire(int rule_idx) const;
};
```

核心设计：**算子数据与子节点分离**。`BoundLogicalOperator` 的 `child` 字段在 Memo 中闲置，子节点通过 `child_groups`（GroupId 列表）引用。`rule_mask` 为位掩码，每个规则对应一个 bit，防止重复触发。

---

## 四、规则系统

### 4.1 PatternNode

```cpp
enum class OptNodeType {
    Scan, LabelScan, Expand, Filter, Project, Aggregate,
    Sort, Skip, Limit, Distinct, CreateNode, CreateEdge,
    Set, Remove, PathBuild, Unwind, BinaryJoin, LeftJoin,
    SemiJoin, Union
};

struct PatternNode {
    OptNodeType type;
    vector<PatternNode> children; // 空 = 子树通配符（匹配任意子树）
};
```

空 `children` 表示叶子通配符，匹配任意子树。例如 `PatternNode{Filter, {{}}}` 表示匹配 Filter 算子且有一个子节点（子树任意）。

### 4.2 OptRule 基类

```cpp
class OptRule {
public:
    virtual ~OptRule() = default;
    virtual string name() const = 0;
    virtual PatternNode pattern() const = 0;

    bool topMatch(const GroupExpr& expr) const;  // O(1) 类型匹配
    virtual bool condition(GroupExpr& expr, Memo& memo) const;       // 条件检查
    virtual vector<unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const = 0; // 生成替代
    virtual int promise() const;  // 优先级：0 = 跳过，越高越先执行
};
```

四段式规则接口：

| 阶段 | 方法 | 作用 |
|------|------|------|
| 快速匹配 | `topMatch()` | 比较算子类型，O(1) |
| 条件检查 | `condition()` | 检查 schema、穿透性等前提 |
| 生成替代 | `substitute()` | 构建新的 GroupExpr 列表 |
| 优先级 | `promise()` | 控制规则调度顺序 |

`condition()` 和 `substitute()` 接受非 const `GroupExpr&`，因为规则需要移动算子数据（`BoundExpression` 含 `unique_ptr` 成员，不可拷贝）。

### 4.3 RuleSet

```cpp
class RuleSet {
public:
    void addRule(unique_ptr<OptRule> rule);
    const vector<unique_ptr<OptRule>>& rules() const;
};
```

注册规则时自动分配 `index()`，与 `GroupExpr::rule_mask` 配合使用。

---

## 五、Task 驱动优化

### 5.1 五种 Task

```
OGroupTask(GroupId, ContextId)
  → CBO: search_circle 检查
  → case 3 (未优化): 创建 null-plan winner，对 Group 内【每个】逻辑 GroupExpr
    推 OExprTask(explore=false)；last_ flag 给最后压栈的那个（最后弹出，
    它的 ~OExprTask 负责收尾）。Columbia 原版只对 first logical expr 推
    O_EXPR，依赖 transformation rule 合流性——我们的 FilterPushdown 不合流
    （Filter→Expand 后没有任何规则能从其他 expr 到达这个新 Expand），
    所以必须遍历全部，参见第九节 Phase 4 修复说明。
  → case 4 (已优化、新 context): 对所有 physical mexpr 推 OInputsTask
    (尚未实现，目前 case 3 路径覆盖了主要场景)

EGroupTask(GroupId, ContextId)
  → 防 re-entrancy：exploring/explored 标志
  → 对 first logical expr 推 OExprTask(explore=true, last=true)

OExprTask(ExprId, explore)
  → 收集 canFire + topMatch + (explore 时排除 isImplementation) 的规则
  → 按 promise 降序排列
  → 逆序压栈（保证最高 promise 的规则先弹出、先触发，匹配 Columbia
    O_EXPR::perform 的 while(--moves>=0) 压栈顺序）
  → last_ flag 给最低 promise 的那条（最后弹出、最后触发，负责收尾）
  → 对所有 child_groups 推 EGroupTask（explore=true 链路）

ApplyRuleTask(RuleIdx, ExprId, explore)
  → canFire / condition 检查
  → substitute 生成新 GroupExpr（可能多个）
  → dispatch 去重：isPhysical ? findPhysDuplicate : findDuplicate
  → 新 expr 的 rule_mask 初始为 0（Columbia 用 Rule->get_mask() 设置
    subsumption mask；我们尚未引入 subsumption 概念，新 expr 不继承
    source 的已触发规则，避免跨算子类型的误抑制）
  → explore=true → 推 OExprTask(new, explore=true)
  → explore=false → 推 OExprTask(new, explore=false) 或 OInputsTask(physical)
  → last_ 通过 is_first_new 透传给首个新 expr 的后续 task

OInputsTask(ExprId, ContextId)
  → 无状态单次扫描：检查全部输入的 winner
    - 全部 done → 计算总代价 + 局部代价，更新 winner
    - 有输入未就绪 → re-push self + 为每个未就绪且未优化的输入推 OGroupTask
    - 有输入已优化但无 winner → 不可能计划，终止
  → 零 arity（叶子）：直接用 findLocalCost 注册 winner
  → 与 Columbia 多次触发版本（InputNo/PrevInputNo/LocalCost/InputCost[]）
    的取舍：实现更简单，代价是每次重入多 O(arity) 次 winner 查询；图查询
    plan arity 通常 1-2，开销可忽略
```

### 5.2 优化循环

```cpp
void LogicalOptimizer::optimize(BoundLogicalPlan& plan) {
    initRules();
    auto root_gid = memo_.copyIn(plan.root);

    TaskQueue queue;
    queue.push(std::make_unique<OGroupTask>(root_gid));

    while (!queue.empty()) {
        auto task = queue.pop();
        task->perform(memo_, rules_, queue);
    }

    plan.root = memo_.copyOut(root_gid);
}
```

`TaskQueue` 使用 `std::deque` 实现栈式（LIFO）调度，与 Columbia 一致。`OGroupTask` 先处理子 Group（bottom-up），确保每个 Group 被优化时其子 Group 已经完成优化。

### 5.3 Bottom-up 优化保证

Bottom-up 由 `OInputsTask` 在 costing 阶段驱动，而非 `OGroupTask` 显式递归：

1. `OGroupTask::perform` search_circle 检查后，对 Group 的**每个** logical expr 推 `OExprTask`（last_ flag 给最后一个，让清理逻辑单次触发）
2. `OExprTask` 收集规则后推 `ApplyRuleTask`，对未 explored 的 child group 推 `EGroupTask`
3. 实现 rule 把 logical expr 转成 physical expr 后，`ApplyRuleTask` 推 `OInputsTask`
4. `OInputsTask::perform` 发现某个输入 Group 没有 winner 且未优化时，re-push 自己 + 推 `OGroupTask` 给该输入；栈式调度保证子 Group 的 winner 先算出，再回到父 Group 继续 costing

LIFO 顺序保证了 `Filter → PathBuild → Expand → LabelScan` 这种链式结构中 LabelScan 先被 costing，自底向上累加代价。

---

## 六、FilterPushdown 规则

### Pattern

```
Filter(LEAF)  — 匹配有一个子节点的 Filter
```

### 策略：单步下推

每次规则触发将 Filter 向下穿透一层。对于多层可穿透算子，Task 循环会为新生成的 GroupExpr 再次触发规则，直到 Filter 到达不可穿透的算子上方。

### 可穿透性

| 算子 | 可穿透 | 原因 |
|------|--------|------|
| Expand | ✅\* | 保留已有列，新增边/目标顶点列 |
| VarLenExpand | ✅\* | 保留已有列，新增 dst/path/edge 列 |
| PathBuild | ✅\* | 保留已有列，新增路径列 |
| Filter | ✅ | 不改变 schema |
| Sort / Skip / Limit / Distinct | ✅ | 不改变 schema |
| Aggregate | ❌ | 改变 schema |
| Project | ❌ | 可能丢弃所需列 |
| LabelScan / Scan | ❌ | 叶子节点，Filter 停在此上方 |

\* 标记的算子会**引入新变量**（如 Expand 的 `edge`/`dst`、PathBuild 的 `path`）。
对于这些算子，下推条件由 `condition()` 中的"引用-新引入重叠检查"决定：

1. 收集 Filter 谓词中引用的所有列名（`collectColumnNames`）
2. 收集子算子**新引入**的变量名（`introducedVariableNames`）——注意：不是算子输出的全部列，只含算子**自己制造**的列。Expand 输出 n/edge/b，但只引入 edge 和 b（n 是从子算子透传的）
3. 若两者有交集，则**禁止下推**——谓词引用了尚未定义的新变量

**不变量**：Filter 谓词只能引用其输入 chunk 中已存在的列。若引用了算子 O 新引入的变量，推到 O 下方会因列缺失导致悬空引用。

**具体例子**：

查询 `MATCH (n:Person)-->(b) WHERE b.age > 18 RETURN n, b`

- Expand 输入 chunk: `{n}`；输出 chunk: `{n, edge, b}`
- `introducedVariableNames(Expand)` = `{edge, b}`（不含 n，n 是透传的）
- `collectColumnNames(b.age > 18)` = `{b}`
- 交集 `{b} ∩ {edge, b}` = `{b}` ≠ ∅ → **禁止下推**

查询 `MATCH (n:Person)-->(b) WHERE n.age > 18 RETURN n, b`

- `introducedVariableNames(Expand)` = `{edge, b}`
- `collectColumnNames(n.age > 18)` = `{n}`
- 交集 `{n} ∩ {edge, b}` = ∅ → **允许下推**。n 在 Expand 之前就存在，推到 Expand 下方 n 列仍在 chunk 里，谓词不依赖 Expand 的新变量，语义等价。

### substitute 过程

```
输入:  Filter(ChildOp(X))
输出:  ChildOp(Filter(X))

1. 从原 Filter 移出 predicate
2. 从子 GroupExpr 移出 child_op 和 grandchild_groups
3. 创建新 Group: Filter(predicate) + grandchild_groups
4. 创建新 GroupExpr: ChildOp → [新 Filter Group]
5. 新 GroupExpr 插入原 Group（与原 Filter 等价）
```

### 示例

```
输入:  Filter → PathBuild → Expand → LabelScan
步骤1: PathBuild → Filter → Expand → LabelScan  (穿透 PathBuild)
步骤2: PathBuild → Expand → Filter → LabelScan  (穿透 Expand)
输出:  PathBuild → Expand → Filter → LabelScan
```

---

## 七、文件结构

```
src/query/optimizer/
├── memo.hpp                 # Memo, Group, GroupExpr, GroupId, ExprId
├── memo.cpp                 # copyIn, copyOut, findDuplicate, mergeGroups
├── opt_rule.hpp             # OptNodeType, PatternNode, OptRule, RuleSet
├── opt_rule.cpp             # nodeTypeFromVariantIndex, topMatch
├── task.hpp                 # Task, TaskQueue, OGroupTask, OExprTask, ApplyRuleTask
├── task.cpp                 # 各 Task::perform 实现
├── logical_optimizer.hpp    # LogicalOptimizer
├── logical_optimizer.cpp    # optimize(), initRules()
└── rules/
    ├── filter_pushdown.hpp  # FilterPushdownRule, isPenetrable
    └── filter_pushdown.cpp
```

测试：`tests/test_optimizer.cpp`

---

## 八、扩展方向

| 规则 | 类型 | 描述 |
|------|------|------|
| FilterPushdown | RBO | 已实现——将 Filter 下推到 Scan 附近 |
| ExpandReorder | RBO | 改变模式匹配的展开起点 |
| ProjectionPruning | RBO | 消除无用列 |
| IndexScanSubstitution | Implementation | Filter(LabelScan) → IndexScan |
| EdgeIndexScanSubstitution | Implementation | Filter(Expand) → EdgeIndexScan |
| CostBasedExpandOrder | CBO | 基于统计信息选择最优展开顺序 |

扩展方式：继承 `OptRule`，在 `LogicalOptimizer::initRules()` 中注册即可，框架代码无需修改。

CBO 扩展需要增加 `E_GROUP` Task（探索子 Group 的所有等价表达式）和 `O_INPUTS` Task（递归优化物理输入并累计代价），Group 内增加 Winner 结构按物理属性缓存最优物理计划。Task 驱动循环不变，只是注册更多规则、Task 种类更丰富。

---

## 九、Columbia 全面对齐路线图

参考来源：`/home/dodo/code/fuck/columbia/cpp/`（特别是 `tasks.cpp`）。整体目标：让优化器真正做代价敏感的搜索，而不是退化为 RBO。共分四期，每期可独立交付，编译/测试均通过。

### 已完成

**Phase 1 — 内容感知的哈希与相等性（已完成）**

修复 Columbia `SSP::FindDup` + `OP::operator==` 对应物缺失导致的去重错误。原本 `Memo::computeHash` 仅按 variant 索引 + child_groups 哈希，两个内容不同的同型算子（如不同 `label_ids` 的两个 `BoundLabelScanOp`）会被错误合并，`MATCH (a), (b)` 退化为自连接。

- 新增：`operator_hash.{hpp,cpp}`、`operator_eq.{hpp,cpp}`，覆盖全部 25 种算子与全部表达式变体
- `Memo::computeHash` 改为接收 `const BoundLogicalOperator&`，混入算子内容
- `Memo::findDuplicate(op, child_groups)` 在桶内调用 `equalBoundLogicalOperator` 二次确认
- `copyOut` 改用 `cloneBoundLogicalOperator`（原 `std::move` 在共享子树下会让第二个父节点拿到空 variant）
- 测试：`MemoContentHash_*` 7 例，覆盖相同/不同 label_ids、相同/不同变量、Filter 谓词相等性、共享子树 copyOut

**Phase 2 — CBO 管线（已完成）**

让 CBO 真正跑起来：根 context 构造、O_INPUTS 多次触发、winner-based copyOut。

- `logical_optimizer.cpp` — 构造根 `Context{PhysProp{}, Cost::infinity()}`，`OGroupTask` 带 `root_ctx_id`，收尾走 `copyOut(root_gid, PhysProp{})`
- `memo.{hpp,cpp}` — 新增 `copyOut(GroupId, const PhysProp&)`：按 PhysProp 查 winner，命中走 winner.plan，未命中回退 RBO
- `task.cpp` — `OInputsTask::perform` 重写为 Columbia 多次触发模式（`input_no_`/`prev_input_no_`/`input_cost_[]`）；`OGroupTask` 重置 `changed`；`~OExprTask` 检查 `group.changed` 决定是否标记 winner done
- `Group::newWinner` 移除错误的 `changed = true`（代价更新不应触发表达式变更标记）
- 测试：`CBOPlumbingTest.*` 5 例，验证 context 创建、winner 流程、多层 plan、copyOut 回退

**Phase 3 — 代价模型 + 逻辑属性（已完成）**

给 Group 一份 cardinality/schema 估计，给物理算子预定义代价公式。

- 新增 `log_prop.{hpp,cpp}` — `ColumnInfo`（variable/column_index/type_kind/label_ids）+ `LogProp`（cardinality/columns/valid）+ `LogPropDeriver` 访问器，覆盖全部 25 种算子，filter 谓词选择性估计（索引 0.01 / 默认 0.1 / 范围 0.3）
- 新增 `cost_model.{hpp,cpp}` — `PhysicalOpTag` 枚举（20 种）+ `findLocalCost`：Scan=output_card、Filter=input_card、HashJoin=build+probe、NLJoin=left×right、Sort=n·log₂(n)
- `catalog.{hpp,cpp}` — 统计访问器 `setLabelStats`/`setEdgeLabelStats`/`getVertexCount`/`getAvgOutDegree`/`hasIndex`，独立于 `LabelDef` 避免破坏序列化
- `memo.{hpp,cpp}` — Group 新增 `log_prop_`/`log_prop_valid_`/`parents_`，懒推导 `getLogProp()` + 递归失效 `invalidateLogProp()`，`insertExpr`/`mergeGroups` 触发失效
- `logical_optimizer.{hpp,cpp}` — `optimize()` 接受可选 `const catalog::Catalog*`
- 测试：`LogPropTest.*` 11 例（cardinality/schema/懒推导/失效传播）+ `CostModelTest.*` 7 例（公式验证）

### 待实施（Phase 4 — 端到端管线已接通，CBO 物理选择已生效）

#### Phase 4 — 实现规则把物理算子接入 Memo

**当前状态**：基础设施 + 端到端管线全部实现（PhysicalExpr、ChosenPlan、GroupExpr 物理 support、19 个实现规则、Memo hash/equality/copyOut dispatch、`PhysicalPlanner::planChosen`、`query_executor` planChosen 分支），46/46 optimizer 测试通过，384/384 query_executor 测试通过，CBO 选出的物理 plan 已在实际查询执行中生效。

**已完成的改动**：
- 新增 `physical_expr.{hpp,cpp}` — `PhysicalExpr{PhysicalOpTag tag, BoundLogicalOperator source}` + `hashPhysicalExpr`/`equalPhysicalExpr`/`clonePhysicalExpr`
- 新增 `chosen_plan.hpp` — `ChosenPlan{tag, op, children}` 树（header-only）
- 新增 `rules/impl/impl_rules.{hpp,cpp}` — 19 个实现规则：Singleton、CorrelatedSource、Scan、LabelScan、Filter、Project、Expand、VarLenExpand、Sort、Limit、Skip、Distinct、Aggregate、PathBuild、Unwind、BinaryJoin、LeftJoin、SemiJoin、Union。每个 override `isImplementation() → true`，`substitute` 克隆源逻辑算子到 PhysicalExpr
- `memo.{hpp,cpp}` — `GroupExpr` 新增 `is_physical_` + `unique_ptr<PhysicalExpr> phys_op_` + 物理构造函数；`computePhysHash`/`findPhysDuplicate` dispatch；`extractChosen(GroupId, PhysProp)` 递归提取 ChosenPlan；`copyOut` 对物理 winner 用 `physOp().source`
- `task.cpp` — `ApplyRuleTask` dispatch dedup on `isPhysical`；`OInputsTask` 重写为无状态单次扫描（检查所有输入 winner，未就绪则 push O_GROUP + re-push self）
- `logical_optimizer.{hpp,cpp}` — `initRules` 注册 19 个 impl 规则 + FilterPushdownRule；`optimize` 调用 `extractChosen` 填充 `plan.chosen`
- `bound_logical_plan.{hpp,cpp}` — 新增 `unique_ptr<ChosenPlan> chosen` 字段（PIMPL 模式，out-of-line 特殊成员函数）；新增 `bound_logical_plan.cpp` 放析构函数
- `cost_model.{hpp}` — PhysicalOpTag 新增 `Singleton`、`CorrelatedSource`
- `physical_planner.{hpp,cpp}` — 新增 `planChosen(const ChosenPlan&, ...)`：内部 `materializeChosen` 把 ChosenPlan 树克隆为 `BoundLogicalOperator` 树（处理 unary `child` 与 binary `left`/`right`），然后委托 `planBoundOperator` 复用全部算子分派
- `query_executor.cpp` — `optimize` 后优先调用 `planChosen`；失败/`chosen == nullptr` 时回退 `planBound`；planChosen 失败走 `spdlog::warn` 而非中断查询
- `CMakeLists.txt` — 注册 `physical_expr.cpp`、`impl_rules.cpp`、`bound_logical_plan.cpp`

**已修复的关键 bug**：
1. **`OptRule::topMatch` 对叶子 pattern 误判**（`opt_rule.cpp`）：原实现在 pattern 为叶子（无 children）时直接返回 true，导致 `ImplCorrelatedSourceRule`、`ImplSingletonRule` 等叶子规则在任何算子上都能触发，污染 group。改为始终比较 root type
2. **零 arity OInputsTask 代价计算**（`task.cpp`）：原实现用 `Cost(0.0)` 注册 winner，使所有叶子算子免费，扭曲下游比较。改为调用 `findLocalCost(tag, group_lp, {})`，LabelScan cost = cardinality
3. **`OGroupTask` 仅对首个 logical expr 推 O_EXPR**（`task.cpp`）：Columbia 原版只对 first logical expr 推 O_EXPR，依赖 transformation rule 的合流性（任何 expr 出发都能通过规则替代到达等价集合）。但我们的 `FilterPushdownRule` 不合流：Filter → Expand 后，没有任何规则能从其他 expr 到达这个新 Expand。导致 E_GROUP 期间新增的 logical expr 永远收不到 optimize 模式的 O_EXPR，其 ImplRule 不会触发，代价永远算不出。修复：`OGroupTask::perform` 遍历 group 的全部 logical_exprs，对每个推 O_EXPR（last_ flag 给最后一个）。参照 Columbia case (4)（已优化 group 在新 context 下重算时对全部 physical expr 推 O_INPUTS）的写法

**当前 planChosen 设计说明**：因为当前每个 impl rule 与一种逻辑算子 1:1 对应（substitute 把源算子整体克隆到 `PhysicalExpr::source`），ChosenPlan 的 `op` 字段变体类型即决定物理算子分派，无需根据 `tag` 分支。未来引入"一逻辑算子多物理算子"（如 BinaryJoin → HashJoin/NLJoin）时，`planChosen` 内需要按 `tag` dispatch；当前实现把这条路径预留好——`materializeChosen` 只重建结构、不做算子分派，分派仍走 `planBoundOperator` 的 visitor。

**测试覆盖**（`tests/test_optimizer.cpp`，51 例）：
- 原有 39 例全部保留（Memo / FilterPushdown / MemoContentHash / CBOPlumbing / LogProp / CostModel）
- 新增 `ImplRule_*` 3 例：LabelScan/Filter 规则 substitute 产物校验、`topMatch` 拒绝不匹配根类型
- 新增 `ChosenPlan_*` 4 例：单 LabelScan plan、Filter→LabelScan 链、materialize 往返、Filter(Expand(LabelScan)) 三层 plan
- 新增 `FrameworkPropertyTest` 5 例：copyIn/copyOut 结构保持、deep plan 收敛、dedup 不变性、确定性、winner 引用合法性

**后续修复（框架正确性对齐）**：
- `Memo::copyOut(gid, prop)` 子节点递归改用 `copyOut(child_gid, PhysProp{})`（先前是无 prop 版本，命中 winner 后子树会回退 RBO，使 plan.root 与 plan.chosen 结构不一致）

### ✅ 已完成（Phase B — Cascades PhysProp + Enforcer 框架）

把 Columbia 的 Enforcer 模式接入 Cascades，让优化器能在代价搜索过程中识别"拓扑形 → 语义形"的属性升级需求并按需插入 Enricher enforcer。这是物理算子审计 (`docs/query/engine/physical-operator-audit.md`) 中 demand-driven Enricher 设计的优化器侧基础。

**当前状态（2026-06）**：数据结构、RequirementCollector、O_INPUTS 的物化感知路径、Memo 的 enforcer 插入 API 全部就位，804/804 单元测试通过。`need_entire` 标志已添加到 MaterializationReq 中用于 RETURN n/e/p 全对象物化判断。column_rewrite pass 已实现（`column_rewrite.cpp/hpp`）。ASAN use-after-free 问题已修复（OInputsTask 中 `localReqdProp` 引用在 `memo.addContext()` 后悬空）。

**已完成的改动（补充 2026-06 新增）**：
- 新增 `materialization_req.hpp` — `MaterializationReq{need_labels, need_props}` + `merge`/`intersect`/`satisfies`/`totalPropertyCount`；`VarRequirements = map<string, MaterializationReq>`；`mergeVarRequirements`/`satisfiesVarRequirements` 自由函数
- 新增 `requirement_collector.{hpp,cpp}` — `collectExprRequirements` 走 BoundExpression 树识别 `BoundPropertyRef`（强类型，emit `need_props[label]={prop}`）/ `BoundDynamicPropertyRef`（弱类型，emit `need_labels`）/ `BoundLabelCast` / `labels()` 函数调用；`collectOpRequirements` 走 BoundLogicalOperator 树，**仅收集本算子自身表达式的需求**（不递归子树），用于后续 impl rule 声明 `required_input_mat`
- `cbo.hpp` — `PhysProp` 新增 `VarRequirements materializations_` + `setMaterializations`/`hasMaterializations`/`materializations`/`satisfies(provider, required)`；`isAny()` 改为同时检查 sort 与 materializations；operator==/!= 覆盖 materializations
- `cost_model.{hpp,cpp}` — `PhysicalOpTag` 新增 `VertexEnrich`/`EdgeEnrich`/`PathEnrich`；`findLocalCost` 新增 Enricher case（粗粒度 = input cardinality，待 LogProp 携带物化宽度后精化）
- `physical_expr.{hpp,cpp}` — `PhysicalExpr` 新增 `enrich_variable`（Enricher 目标变量名）/ `output_mat`（本算子提供的物化）/ `required_input_mat`（per-input 声明，下标=child position）；`hashPhysicalExpr`/`equalPhysicalExpr`/`clonePhysicalExpr` 全部覆盖新字段
- `chosen_plan.hpp` — `ChosenPlan` 新增 `enrich_variable`/`enrich_output`，供 `PhysicalPlanner::planChosen` 识别 Enricher 节点
- `memo.{hpp,cpp}` — `Group` 新增 `requirements_`/`requirements_valid_` + `setRequirements`/`requirements`/`requirementsValid`；`Memo::copyIn` 在创建新 group 时调用 `collectOpRequirements` 自动填充；新增 `Group::getSatisfyingWinner`（最便宜的满足 prop 的 done winner）；新增 `Memo::insertEnricherEnforcer(g, req_mat)` 按 LogProp.columns 推断变量类型（Vertex/Edge/Path），per-variable 插入对应 Enricher 物理 GroupExpr（child_groups=[g]）；`PhysProp::dump` 输出 mat{...}；`extractChosen` 改用 `getSatisfyingWinner` + 递归时按算子 `required_input_mat[i]` ∪ `parent_mat` 计算子节点 prop
- `task.cpp` — `OInputsTask::perform` 改为物化感知：(1) 算 input required prop = `expr.required_input_mat[i]` ∪ `ctx.materializations`；(2) 先查 `getSatisfyingWinner(inputProp)`，若 input.optimized 仍无则调 `insertEnricherEnforcer` 试图插入 enforcer 包装 any-winner；(3) 注册 winner 前校验 `expr.output_mat` 满足 `ctx.materializations`（拓扑形叶子在要求物化的 context 下不再误注册 winner，由 enforcer 路径补救）；`OGroupTask::perform` 新增 case 4 分支：已 optimized 的 group 收到新 context 时对全部 physical_exprs 推 O_INPUTS（无 physical_exprs 时回退 O_EXPR）

**当前限制（2026-06）**：
- PropertyExtract 算子（Vertex/Edge/Path）已实现但未接入执行管线。`planChosen` 中通过 `hasPropertyExtract(chosen)` 判断是否激活 standalone 模式；当前 CBO 不产生 PropertyExtract 标签，因此仍走旧 wrap 管线。`PhysicalOpTag` 新增了 `VertexPropertyExtract`/`EdgePropertyExtract`/`PathPropertyExtract`。
- Enricher（VertexEnrich/EdgeEnrich/PathEnrich）physical operator 尚未实现——当前由 RBO wrap 管线（wrapVertexLabelRead / wrapVertexPropertyRead / wrapEdgePropertyRead / wrapPathElementPropertyRead）原地升级拓扑类型。
- `need_entire` 标志已添加到 MaterializationReq 中，用于 RETURN n/e/p 全对象物化判断。`insertEnricherEnforcer` 根据 `need_entire` 决定插入 PropertyExtract 还是 Enricher。
- column_rewrite pass 已实现（`column_rewrite.cpp/hpp`），用于将 BoundPropertyRef 替换为 BoundColumnRef，配合 standalone PropertyExtractPhysicalOp 的列追加模式。
- `OInputsTask` 中 ASAN use-after-free 已修复（`localReqdProp` 引用在 `memo.addContext()` 后悬空，改为拷贝）。
- `OExprTask` 中空 if-body 编译警告已修复。
- 路径函数 `nodes()`/`relationships()`/`length()` 已支持 PathTopology 类型。

**测试覆盖**（`tests/test_optimizer.cpp`，共 82 例）：
- 原有 61 例全部保留
- 新增 `MaterializationReqTest.*` 5 例（empty/merge/satisfies/intersect/VarRequirements 辅助函数）
- 新增 `PhysPropTest.*` 4 例（any/hasMaterializations/satisfies/sort+mat 复合/dump）
- 新增 `RequirementCollectorTest.*` 4 例（强 PropertyRef、动态 PropertyRef、BinaryOp 递归、Filter own-requirements 不含子树）
- 新增 `MemoRequirementsTest.CopyInPopulatesRequirementsField` — 验证 copyIn 自动填充
- 新增 `EnricherEnforcerTest.*` 2 例（无 any-winner 时返回 INVALID、getSatisfyingWinner 选最便宜）
- 新增 `PhysicalExprTest.*` 2 例（hash/eq 包含 enricher 字段、clone 保留）
- 新增 `CostModelTest.EnricherCostProportionalToInputCardinality` — 三个 Enricher tag 等价且 > 0

**下一步（Phase C）**：
1. 在 `rules/impl/` 各 impl rule 的 `substitute` 中填充 `required_input_mat[0]` = `group.requirements()`（该算子自身声明）
2. 让 Scan/Expand 等 impl rule 的 `output_mat` 默认为空（拓扑形），以便 enforcer 路径在父算子要求物化时自动插入 Enricher
3. 实现真正的 `VertexEnrich`/`EdgeEnrich`/`PathEnrich` 物理 operator（runtime 侧），由 `PhysicalPlanner::planChosen` 在识别到对应 tag 时构建
4. `findLocalCost` Enricher 公式精化（input_card × (1 + total_prop_count + has_labels)）

### 已知 Columbia 框架差距（待办，非正确性 bug）

逐条核对 Columbia `tasks.cpp` / `expr.cpp` / `rules.cpp` 后确认的五项差距，按"是否影响正确性 / 当前规则集是否触发"分级：

| # | 项 | 判定 | 当前影响 | 触发条件 |
|---|-----|------|---------|---------|
| 1 | `copyOut(gid, prop)` 子节点递归不走 winner | **已修复**（正确性 bug） | — | — |
| 2 | `O_GROUP::perform` case 4（已优化 + 新 context 重算）缺失 | 仅剪枝缺失 | 单 context 用法永不触发 | 引入按 PhysProp 需求优化（如 ORDER BY 要求 sorted）时触发 |
| 3 | `O_INPUTS::perform` 上下界收紧缺失 | 仅剪枝缺失 | 性能，不影响正确性 | 总是（搜索空间更大） |
| 4 | `BINDERY` 多 binding 枚举 | 仅覆盖度问题 | 当前规则集 pattern 简单，无漏匹配 | 引入多 join 顺序探索等复杂 transformation rule 时 |
| 5 | Rule subsumption mask 缺失 | 仅剪枝缺失 | 当前规则间无 subsumption 关系 | 引入 Columbia-style join commutativity / associativity 规则群时 |

补 #2-#5 的时机：等真用到对应场景（多 context 优化、复杂 transformation rules）再做，现在补属于 YAGNI。

**算子级推迟项**：sort 强制规则（`OrderBy → Sort` 配合属性传播）、索引选择、真正的 BINDERY 多绑定、替代物理算子（HashJoin/NLJoin 区别于 CrossProduct）。引入替代算子时 `ImplBinaryJoinRule` 需同时发射多个 `PhysicalExpr`、`planChosen` 需按 `tag` 分派。

### 全面 Columbia 对齐核对（已完成）

逐文件对照 Columbia 源码目录 `/home/dodo/code/fuck/columbia/cpp/` 与本目录，确认框架**正确性层**已完全对齐，剩余差距全部是能力增强而非正确性 bug。

**控制流层（tasks.cpp / rules.cpp / query.cpp / cat.cpp）** — 100% 覆盖 Columbia 五种 Task 与调度规则：
- ✅ PTASKS LIFO 栈 + Last flag 传播
- ✅ O_GROUP search_circle 4 case（memo.cpp:866-906 完整实现 const/winner/first/re-optimize）
- ✅ O_EXPR promise 排序 + 子节点 E_GROUP 派发
- ✅ APPLY_RULE substitute → dedup → explore/optimize 分支派发
- ✅ E_GROUP exploring/explored 去重
- ✅ RULE 基类接口（pattern / top_match / promise / condition / substitute）
- ✅ Catalog 统计接口
- ✅ QUERY 顶层入口流程

**数据结构层（ssp.cpp / group.cpp / mexpr.cpp / expr.cpp / physop.cpp / logop.cpp / cm.cpp / item.cpp）** — 核心骨架完整：
- ✅ SSP ↔ Memo（FindDup / HashTbl / MergeGroups / CopyIn / CopyOut / extractChosen）
- ✅ GROUP 状态机（exploring/explored/optimizing/optimized）+ Winner's circle（额外：parents_ 集合做 LogProp 失效传播）
- ✅ M_EXPR ↔ GroupExpr（rule_mask / child_groups / hash）
- ✅ LOG_PROP ↔ LogProp（cardinality + schema + 列信息）
- ✅ ITEM ↔ operator_hash/operator_eq（内容感知 hash/equality）
- ⚠️ PHYS_PROP — 当前仅 SortOrder（any/asc/desc），Columbia 有 Order × Keys × KeyOrder + InputReqdProp 传播。要 sort-aware CBO 时再补。
- ⚠️ CM — 当前单值 Cost，Columbia 有 CPU/IO/mem 多维常量。要更精确 cost 时再补。
- ⚠️ ITEM::FindLogProp — 表达式级 selectivity 估算缺失，当前用默认常量。要精确 Filter 选择率时再补。

**结论**：优化器框架与 Columbia 已对齐。剩余 ⚠️ 项全是"等用到再补"的能力增强（PHYS_PROP 多 key 传播、多维 Cost、表达式选择率），不影响框架正确性。`wcol.cpp`（Windows GUI）、`supp.cpp`（hash 工具）、`expr.cpp`（EXPR 树节点）属于 Columbia 内部实现细节，已被 eugraph 用 std::unordered_map / BoundLogicalOperator 内嵌树替代，无需对齐。

**测试覆盖**（`tests/test_optimizer.cpp`，61 例）：
- 数据结构 / 行为单元测试 46 例（Memo / FilterPushdown / MemoContentHash / CBOPlumbing / LogProp / CostModel / ImplRule / ChosenPlan）
- `FrameworkPropertyTest` 5 例：copyIn/copyOut 结构保持、deep plan 收敛、dedup 不变性、确定性、winner 引用合法性
- `SearchCircleTest` 4 例：search_circle 4 case（首次/winner 满足/winner 太贵/null winner + 宽松 UB）
- `PatternMatchTest` 3 例：topMatch 仅检查 root、topMatch 忽略 pattern.children、impl rule pattern 形状契约
- `TaskQueueTest` 1 例：LIFO 顺序 + 空 queue 返回 nullptr
- `GroupMergeTest` 2 例：mergeGroups 后 child_groups 引用重路由、logical_exprs 取并集

### 关键文件

每期都会动的文件：

- `src/query/optimizer/memo.{hpp,cpp}`
- `src/query/optimizer/task.cpp`（Phase 2/4）
- `src/query/optimizer/logical_optimizer.cpp`（Phase 2/4）
- `src/query/optimizer/cbo.hpp`（Phase 3）
- `src/query/physical_plan/physical_planner.cpp`（Phase 4）

### 复用现有代码

- `cloneBoundLogicalOperator`（`memo.cpp:341-543`）—— 哈希/相等性/LogProp 访客的字段清单蓝本
- `cloneBoundExpression`（`memo.cpp:557+`）—— 表达式对应物
- `PhysicalPlanner::planBound`（`physical_planner.cpp:555`）—— 实现规则族参考
- `OptRule::isImplementation()`（`opt_rule.hpp:85`）—— 已声明，实现规则 override 为 `true`

### 验证策略

每期完成后：

1. `cmake --build build -j$(nproc)`
2. `ctest --test-dir build -j4 -E tck_tests`（全量单元测试，目前 700 例）
3. 新增单元测试按期归类加入 `tests/test_optimizer.cpp`
4. Phase 4 额外跑端到端冒烟：小计划优化 → `ChosenPlan` 由 `PhysicalPlanner` 消费 → `query_executor_tests` 跑同一条 query → 结果与 RBO 路径一致
