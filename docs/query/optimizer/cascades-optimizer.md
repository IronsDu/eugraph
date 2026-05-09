# Cascades 逻辑优化器

> 相关模块：Optimizer、Binder、PhysicalPlanner、QueryExecutor
> 源码：`src/compute_service/optimizer/`

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
    Set, Remove, PathBuild
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

### 5.1 三种 Task

```
OGroupTask(GroupId)
  → 先递归优化子 Group（bottom-up）
  → 为 Group 内每个逻辑 GroupExpr 创建 OExprTask

OExprTask(ExprId)
  → 收集所有可触发且 topMatch 的规则
  → 按 promise 降序排列
  → 为每个创建 ApplyRuleTask

ApplyRuleTask(RuleIdx, ExprId)
  → 检查 condition
  → 调用 substitute 生成新 GroupExpr
  → 插入 Memo（去重、必要时合并 Group）
  → 为新 GroupExpr 及其子 Group push 后续 OExprTask
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

`OGroupTask::perform` 的执行顺序：

1. 遍历 Group 内所有 GroupExpr 的 `child_groups`
2. 对未优化的子 Group 标记 `optimized` 并 push `OGroupTask`（栈式调度，子 Group 先执行）
3. 再为当前 Group 的所有 GroupExpr push `OExprTask`

这保证了深层节点先被优化，例如 `Filter → PathBuild → Expand → LabelScan` 中 Expand 的子 Group 先完成优化，Filter 的规则触发时子树已处于最优状态。

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
| Expand | ✅ | 保留已有列，新增边/目标顶点列 |
| PathBuild | ✅ | 保留已有列，新增路径列 |
| Filter | ✅ | 不改变 schema |
| Sort / Skip / Limit / Distinct | ✅ | 不改变 schema |
| Aggregate | ❌ | 改变 schema |
| Project | ❌ | 可能丢弃所需列 |
| LabelScan / Scan | ❌ | 叶子节点，Filter 停在此上方 |

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
src/compute_service/optimizer/
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
