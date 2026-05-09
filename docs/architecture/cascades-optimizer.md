# Cascades 逻辑优化器设计

> 状态：开发中
> 相关模块：Optimizer、Binder、PhysicalPlanner、QueryExecutor
> 分支：feat/cascades-optimizer

---

## 一、设计动机

当前查询执行管线为 `Binder → PhysicalPlanner`，没有独立的优化阶段。存在以下问题：

1. **Filter 位置不合理**：`MATCH (n:person)-[]->(m) WHERE n.firstName = 'Mahinda' RETURN p` 生成的计划为 `LabelScan → Expand → PathBuild → Filter`，Filter 在 Expand 之后。但谓词只依赖 LabelScan 产出的 `n` 列，应在 Expand 之前过滤
2. **无模式匹配起点优化**：同一 path pattern `(a)-[]->(b)-[]->(c)` 可从 a、b、c 任意一端开始展开，不同起点性能差异可达数量级
3. **缺少可扩展的优化框架**：当前的 `applyProjectionPushdown` 硬编码在 Binder 中，无法系统化地添加新优化规则

### 为何选择 Cascades 框架

图查询的模式匹配优化本质是**搜索空间枚举**问题——同一语义的查询可能对应多种逻辑计划（不同展开起点、不同 Filter 位置、不同 Join 顺序）。Cascades 框架的设计目标正是：

- **Memo** 以 Group 为单位组织等价计划，天然支持搜索空间的紧凑存储和去重
- **Task 驱动**将优化过程分解为可调度、可剪枝的细粒度任务，为代价敏感的搜索策略留出空间
- **Rule 接口**将"匹配什么"和"如何变换"解耦，新增规则无需修改框架代码

### 参考来源：Columbia Optimizer Framework

本设计参考 [Columbia Optimizer](https://github.com/yongwen/columbia)（Portland State University / Oregon Graduate Institute）的核心架构。Columbia 是 Cascades 论文的经典教学实现，代码量适中、结构清晰。

**参考 Columbia 的核心价值**：

| Columbia 组件 | 参考价值 | 本项目对应 |
|---|---|---|
| SSP（Search Space） | Memo 以 Group + GroupExpr 组织等价表达式 | `Memo` |
| M_EXPR | 算子数据与子节点分离，用 GroupId 引用子节点 | `GroupExpr` |
| RULE + BINDERY | 四段式规则接口：top_match → bind → condition → substitute | `OptRule` |
| PTASKS + TASK 子类 | Task 驱动的探索/优化循环 | `TaskQueue` + 4 种 Task |
| LEAF_OP | Pattern 中的 Group 占位符 | `PatternLeaf` |
| WINNER | Group 内的最优计划缓存 | 未来 CBO 使用 |

**不参考的部分**：Columbia 使用 90 年代 MFC 风格 C++（裸指针、CArray、CString、手动内存管理）。本项目使用现代 C++（std::unique_ptr、std::variant、std::vector、std::deque）重新实现。

---

## 二、整体管线

```
Binder → LogicalOptimizer (Cascades) → PhysicalPlanner
```

优化器插入位置：`query_executor.cpp` 中 `binder.bind()` 之后、`physical_planner.planBound()` 之前。

```
                                   ┌──────────────────┐
                                   │   LogicalOptimizer│
                                   └────────┬─────────┘
                                            │
                    ┌───────────────────────┼───────────────────────┐
                    │                       │                       │
             ┌──────▼──────┐        ┌──────▼──────┐        ┌──────▼──────┐
             │   copyIn    │        │   explore   │        │   copyOut  │
             │ (树 → Memo) │        │ (Task驱动)  │        │ (Memo → 树)│
             └─────────────┘        └─────────────┘        └─────────────┘
```

---

## 三、核心数据结构

### 3.1 Memo（对应 Columbia SSP）

```cpp
// src/compute_service/optimizer/memo.hpp

using GroupId = uint32_t;
using ExprId = uint32_t;
constexpr GroupId INVALID_GROUP_ID = UINT32_MAX;

class Memo {
public:
    GroupId copyIn(binder::BoundLogicalOperator& op);
    binder::BoundLogicalOperator copyOut(GroupId root_gid);

    GroupExpr* findDuplicate(const GroupExpr& expr);
    GroupId mergeGroups(GroupId g1, GroupId g2);

    Group& getGroup(GroupId id);
    GroupExpr& getExpr(ExprId id);
    GroupId newGroupId();

private:
    std::vector<std::unique_ptr<Group>> groups_;
    std::vector<std::unique_ptr<GroupExpr>> exprs_;
    GroupId next_group_id_ = 0;
    ExprId next_expr_id_ = 0;
};
```

### 3.2 Group（对应 Columbia GROUP）

```cpp
class Group {
public:
    GroupId id;
    std::vector<ExprId> logical_exprs;
    std::vector<ExprId> physical_exprs;  // CBO 阶段使用

    bool explored = false;
    bool optimized = false;
};
```

### 3.3 GroupExpr（对应 Columbia M_EXPR）

核心设计：**算子数据与子节点分离**。`BoundLogicalOperator` 的 `child` 字段在 Memo 中闲置，子节点通过 `child_groups`（GroupId 列表）引用。

```cpp
class GroupExpr {
public:
    ExprId id;
    GroupId group_id;

    binder::BoundLogicalOperator op;     // child 字段闲置
    std::vector<GroupId> child_groups;   // 子节点 Group 引用

    uint32_t rule_mask = 0;              // 已应用规则位掩码

    void markRuleFired(int rule_idx);
    bool canFire(int rule_idx) const;
};
```

---

## 四、规则系统（对应 Columbia RULE）

### 4.1 OptRule 基类

```cpp
struct PatternNode {
    std::optional<OptNodeType> op_type;  // 空 = 任意算子/叶子
    std::optional<int> leaf_index;       // 非空 = 叶子占位符
    std::vector<PatternNode> children;
};

class OptRule {
public:
    virtual ~OptRule() = default;
    virtual std::string name() const = 0;
    virtual PatternNode pattern() const = 0;

    bool topMatch(const GroupExpr& expr, const Memo& memo) const;
    virtual bool condition(const GroupExpr& expr, const Memo& memo) const { return true; }

    virtual std::vector<std::unique_ptr<GroupExpr>>
    substitute(const GroupExpr& expr, Memo& memo) const = 0;

    virtual int promise() const { return 1; }
};
```

**四段式规则接口**（源自 Columbia）：

| 阶段 | 方法 | 作用 | 时机 |
|------|------|------|------|
| 快速匹配 | `topMatch()` | 只比算子类型，O(1) | O_EXPR task 中 |
| 条件检查 | `condition()` | 绑定后检查 schema、列依赖等 | APPLY_RULE task 中 |
| 生成替代 | `substitute()` | 构建新的 GroupExpr | APPLY_RULE task 中 |
| 优先级 | `promise()` | 控制规则调度顺序 | O_EXPR task 中 |

### 4.2 RBO 规则实现方式

RBO 规则的特点：确定性变换，1 个输入 → 1 个输出。实现时：

1. `pattern()` 声明匹配结构（如 `Filter(LEAF(0))`）
2. `condition()` 检查变换前提（如谓词依赖的列在子树中已存在）
3. `substitute()` 构建变换后的新 GroupExpr
4. `promise()` 返回固定值（RBO 不基于代价调整）

**RBO 规则不涉及代价**。Group 内的多个 GroupExpr 由规则依次产生，RBO 阶段取最后一个（即最新变换结果）作为"最优"。

### 4.3 未来 CBO 规则实现方式

CBO 规则与 RBO 规则共享同一个 `OptRule` 接口，区别在于：

1. **实现规则**（Implementation Rule）：将逻辑算子转为物理算子（如 `LabelScan → IndexScan`），`substitute()` 返回的 GroupExpr 中 op 为物理算子
2. **代价感知优先级**：`promise()` 基于统计信息动态返回值，代价高的替代优先级低
3. **Winner 机制**：Group 内新增 `Winner` 结构，按物理属性（排序、分布）缓存最优物理计划
4. **剪枝**：O_INPUTS task 中计算代价上界，超过当前最优时跳过剩余替代

```
RBO 阶段：LogicalExpr → LogicalExpr（结构变换）
CBO 阶段：LogicalExpr → PhysicalExpr（实现选择）+ 代价比较
```

**扩展方式**：只需在 RuleSet 中注册新规则，框架代码无需修改。

---

## 五、Task 驱动探索（对应 Columbia PTASKS）

### 5.1 四种 Task

```
O_GROUP(GroupId)
  → 遍历 Group 内所有逻辑 GroupExpr
  → 为每个创建 O_EXPR task

O_EXPR(ExprId, bool explore)
  → 找到所有 topMatch 的规则
  → 按 promise 降序排列
  → 为每个创建 APPLY_RULE task

E_GROUP(GroupId)
  → 对 Group 内每个逻辑 GroupExpr 创建 O_EXPR(explore=true)
  → 只触发逻辑变换规则（不触发实现规则）

APPLY_RULE(RuleIdx, ExprId)
  → 绑定 pattern
  → 检查 condition
  → 调用 substitute 生成新 GroupExpr
  → 插入 Memo（去重、必要时合并 Group）
  → 为新 GroupExpr 的子 Group push E_GROUP task
```

### 5.2 RBO 阶段的 Task 流程

RBO 阶段只需要 O_GROUP + O_EXPR + APPLY_RULE（不需要 E_GROUP，因为没有多路径探索）：

```
1. copyIn(plan.root) → root_gid
2. push(O_GROUP(root_gid))
3. while (!queue.empty()):
     task = queue.pop()
     task.perform(memo, rules)
4. plan.root = copyOut(root_gid)
```

### 5.3 未来 CBO 阶段的扩展

CBO 需要增加 E_GROUP 和 O_INPUTS：

- **E_GROUP**：在应用变换规则前，先探索子 Group 的所有等价表达式
- **O_INPUTS**：对物理 GroupExpr 的每个输入 Group 递归优化，累计代价
- **Winner**：Group 内按物理属性缓存最优物理计划，避免重复搜索

Task 驱动循环不变，只是注册的规则更多、Task 种类更丰富。

---

## 六、第一个规则：FilterPushdown

### Pattern

```
Filter(LEAF(0))
```

### 算法

1. `topMatch`：检查 GroupExpr 是否为 Filter 算子
2. `condition`：从谓词中提取依赖的列名集合，确认子树中存在这些列
3. `substitute`：沿子树向下找到最低可穿透点，在下方插入 Filter

**可穿透性判断**：

| 算子 | 可穿透 | 原因 |
|------|--------|------|
| Expand | ✅ | 保留已有列，新增边/目标顶点列 |
| PathBuild | ✅ | 保留已有列，新增路径列 |
| Filter | ✅ | 不改变 schema |
| Sort / Skip / Limit / Distinct | ✅ | 不改变 schema |
| Aggregate | ❌ | 改变 schema（分组/聚合后的 schema） |
| Project | ❌ | 可能丢弃所需列 |
| LabelScan / Scan | 到达叶子 | 在此上方插入 Filter |

### 示例

```
输入:  Filter → PathBuild → Expand → LabelScan
输出:  PathBuild → Expand → Filter → LabelScan
```

---

## 七、文件结构

```
src/compute_service/optimizer/
├── memo.hpp                     # Memo, Group, GroupExpr
├── memo.cpp
├── opt_rule.hpp                 # OptRule, PatternNode, RuleSet
├── task.hpp                     # Task, TaskQueue, O_GROUP, O_EXPR, E_GROUP, APPLY_RULE
├── task.cpp
├── logical_optimizer.hpp        # LogicalOptimizer
├── logical_optimizer.cpp
└── rules/
    ├── filter_pushdown.hpp
    └── filter_pushdown.cpp
```

---

## 八、后续规则扩展

| 规则 | 类型 | 描述 |
|------|------|------|
| FilterPushdown | RBO | 将 Filter 下推到 Scan 附近 |
| ExpandReorder | RBO | 改变模式匹配的展开起点 |
| ProjectionPruning | RBO | 消除无用列 |
| IndexScanSubstitution | Implementation | Filter(LabelScan) → IndexScan（可从 PhysicalPlanner 迁移） |
| EdgeIndexScanSubstitution | Implementation | Filter(Expand) → EdgeIndexScan（可从 PhysicalPlanner 迁移） |
| CostBasedExpandOrder | CBO | 基于统计信息选择最优展开顺序 |
