#include "compute_service/logical_plan/logical_plan_builder.hpp"

#include <sstream>
#include <stdexcept>

namespace eugraph {
namespace compute {

// ==================== Public API ====================

std::variant<LogicalPlan, std::string> LogicalPlanBuilder::build(cypher::Statement stmt) {
    if (std::holds_alternative<std::unique_ptr<cypher::RegularQuery>>(stmt)) {
        auto& query = std::get<std::unique_ptr<cypher::RegularQuery>>(stmt);
        if (!query->unions.empty()) {
            return "UNION is not yet supported";
        }
        auto result = buildSingleQuery(query->first);
        if (std::holds_alternative<std::string>(result)) {
            return std::get<std::string>(result);
        }
        return LogicalPlan{std::move(std::get<LogicalOperator>(result))};
    }

    return "Unsupported statement type";
}

// ==================== Helpers ====================

uint32_t LogicalPlanBuilder::ensureVariable(const std::string& name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return it->second;
    }
    uint32_t idx = next_column_++;
    symbols_[name] = idx;
    return idx;
}

// ==================== SingleQuery ====================

std::variant<LogicalOperator, std::string> LogicalPlanBuilder::buildSingleQuery(cypher::SingleQuery& query) {
    std::optional<LogicalOperator> current;

    for (auto& clause : query.clauses) {
        if (std::holds_alternative<std::unique_ptr<cypher::MatchClause>>(clause)) {
            auto& match = std::get<std::unique_ptr<cypher::MatchClause>>(clause);
            auto matchOp = buildMatch(*match);
            if (std::holds_alternative<std::string>(matchOp)) {
                return std::get<std::string>(matchOp);
            }
            auto matchRoot = std::move(std::get<LogicalOperator>(matchOp));

            if (current.has_value()) {
                return "Multiple MATCH clauses not yet supported";
            }
            current = std::move(matchRoot);

        } else if (std::holds_alternative<std::unique_ptr<cypher::ReturnClause>>(clause)) {
            if (!current.has_value()) {
                return "RETURN without preceding MATCH/CREATE";
            }
            auto& ret = std::get<std::unique_ptr<cypher::ReturnClause>>(clause);
            auto result = buildReturn(*ret, std::move(*current));
            if (std::holds_alternative<std::string>(result)) {
                return std::get<std::string>(result);
            }
            current = std::move(std::get<LogicalOperator>(result));

        } else if (std::holds_alternative<std::unique_ptr<cypher::CreateClause>>(clause)) {
            auto& create = std::get<std::unique_ptr<cypher::CreateClause>>(clause);
            auto result = buildCreate(*create);
            if (std::holds_alternative<std::string>(result)) {
                return std::get<std::string>(result);
            }
            if (current.has_value()) {
                return "CREATE after other clauses not yet supported";
            }
            current = std::move(std::get<LogicalOperator>(result));

        } else {
            return "Clause type not yet supported in this phase";
        }
    }

    if (!current.has_value()) {
        return "Empty query";
    }
    return std::move(*current);
}

// ==================== MATCH ====================

std::variant<LogicalOperator, std::string> LogicalPlanBuilder::buildMatch(cypher::MatchClause& match) {
    if (match.patterns.empty()) {
        return "MATCH with no patterns";
    }

    std::optional<LogicalOperator> root;

    for (auto& part : match.patterns) {
        auto elemOp = buildPatternElement(part.element);
        if (std::holds_alternative<std::string>(elemOp)) {
            return std::get<std::string>(elemOp);
        }
        auto elemRoot = std::move(std::get<LogicalOperator>(elemOp));

        if (!root.has_value()) {
            root = std::move(elemRoot);
        } else {
            return "Multiple pattern parts in MATCH not yet supported";
        }
    }

    if (match.where_pred.has_value()) {
        auto filterResult = buildFilter(std::move(*match.where_pred), std::move(*root));
        if (std::holds_alternative<std::string>(filterResult)) {
            return std::get<std::string>(filterResult);
        }
        root = std::move(std::get<LogicalOperator>(filterResult));
    }

    return std::move(*root);
}

// ==================== PatternElement ====================

std::variant<LogicalOperator, std::string> LogicalPlanBuilder::buildPatternElement(cypher::PatternElement& elem) {
    auto& node = elem.node;
    std::string nodeVar = node.variable.value_or("");

    LogicalOperator scanOp;
    if (node.labels.empty()) {
        auto op = std::make_unique<AllNodeScanOp>();
        op->variable = nodeVar;
        if (!nodeVar.empty()) {
            ensureVariable(nodeVar);
        }
        scanOp = std::move(op);
    } else {
        auto op = std::make_unique<LabelScanOp>();
        op->variable = nodeVar;
        op->label = node.labels[0];
        if (!nodeVar.empty()) {
            ensureVariable(nodeVar);
        }
        scanOp = std::move(op);
    }

    LogicalOperator current = std::move(scanOp);

    for (auto& [rel, nextNode] : elem.chain) {
        std::string dstVar = nextNode.variable.value_or("");
        std::string edgeVar = rel.variable.value_or("");

        if (!dstVar.empty()) {
            ensureVariable(dstVar);
        }
        if (!edgeVar.empty()) {
            ensureVariable(edgeVar);
        }

        auto expandOp = std::make_unique<ExpandOp>();
        expandOp->src_variable = nodeVar;
        expandOp->dst_variable = dstVar;
        expandOp->edge_variable = edgeVar;
        expandOp->rel_types = std::move(rel.rel_types);
        expandOp->direction = rel.direction;
        expandOp->range = std::move(rel.range);
        expandOp->children.push_back(std::move(current));
        current = std::move(expandOp);

        nodeVar = dstVar;
    }

    return current;
}

// ==================== RETURN ====================

namespace {
bool isAggregateFunction(const std::string& name) {
    return name == "count" || name == "sum" || name == "avg" || name == "min" || name == "max";
}

bool containsAggregate(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using T = std::decay_t<decltype(ptr)>;
            using OpType = typename T::element_type;
            if constexpr (std::is_same_v<OpType, cypher::FunctionCall>) {
                return isAggregateFunction(ptr->name);
            }
            return false;
        },
        expr);
}
} // namespace

std::variant<LogicalOperator, std::string> LogicalPlanBuilder::buildReturn(cypher::ReturnClause& ret,
                                                                           LogicalOperator input) {
    LogicalOperator current;

    // Check if any RETURN item contains an aggregate function
    bool has_aggregate = false;
    if (!ret.return_all) {
        for (const auto& item : ret.items) {
            if (containsAggregate(item.expr)) {
                has_aggregate = true;
                break;
            }
        }
    }

    if (has_aggregate) {
        // Build AggregateOp instead of ProjectOp
        auto aggregate = std::make_unique<AggregateOp>();
        std::vector<std::string> output_names;

        for (auto& retItem : ret.items) {
            if (std::holds_alternative<std::unique_ptr<cypher::FunctionCall>>(retItem.expr)) {
                auto& fc = std::get<std::unique_ptr<cypher::FunctionCall>>(retItem.expr);
                if (isAggregateFunction(fc->name)) {
                    // Compute name before moving from fc->args
                    std::string name = retItem.alias.value_or(cypher::expressionToString(retItem.expr));
                    AggregateFunc af;
                    af.func_name = fc->name;
                    af.distinct = fc->distinct;
                    if (fc->args.empty()) {
                        af.arg = cypher::makeLiteral(cypher::NullValue{});
                    } else {
                        af.arg = std::move(fc->args[0]);
                    }
                    output_names.push_back(name);
                    aggregate->aggregates.push_back(std::move(af));
                    continue;
                }
            }
            // Non-aggregate item → group key
            output_names.push_back(retItem.alias.value_or(cypher::expressionToString(retItem.expr)));
            aggregate->group_keys.push_back(std::move(retItem.expr));
        }

        aggregate->output_names = std::move(output_names);
        aggregate->children.push_back(std::move(input));
        current = std::move(aggregate);
    } else {
        auto project = std::make_unique<ProjectOp>();

        if (ret.return_all) {
            for (const auto& [name, idx] : symbols_) {
                ProjectOp::ProjectItem item;
                item.expr = cypher::makeVariable(name);
                item.alias = name;
                project->items.push_back(std::move(item));
            }
        } else {
            for (auto& retItem : ret.items) {
                ProjectOp::ProjectItem item;
                item.expr = std::move(retItem.expr);
                item.alias = std::move(retItem.alias);
                project->items.push_back(std::move(item));
            }
        }

        project->distinct = ret.distinct;
        project->children.push_back(std::move(input));
        current = std::move(project);
    }

    // ORDER BY
    if (ret.order_by.has_value()) {
        auto sortOp = std::make_unique<SortOp>();
        sortOp->sort_items = std::move(ret.order_by->items);
        sortOp->children.push_back(std::move(current));
        current = std::move(sortOp);
    }

    // SKIP
    if (ret.skip.has_value()) {
        int64_t skipVal = 0;
        if (std::holds_alternative<std::unique_ptr<cypher::Literal>>(*ret.skip)) {
            auto& lit = std::get<std::unique_ptr<cypher::Literal>>(*ret.skip);
            if (std::holds_alternative<int64_t>(lit->value)) {
                skipVal = std::get<int64_t>(lit->value);
            }
        }
        auto skipOp = std::make_unique<SkipOp>();
        skipOp->skip = skipVal;
        skipOp->children.push_back(std::move(current));
        current = std::move(skipOp);
    }

    // LIMIT
    if (ret.limit.has_value()) {
        int64_t limitVal = 0;
        if (std::holds_alternative<std::unique_ptr<cypher::Literal>>(*ret.limit)) {
            auto& lit = std::get<std::unique_ptr<cypher::Literal>>(*ret.limit);
            if (std::holds_alternative<int64_t>(lit->value)) {
                limitVal = std::get<int64_t>(lit->value);
            }
        }
        auto limitOp = std::make_unique<LimitOp>();
        limitOp->limit = limitVal;
        limitOp->children.push_back(std::move(current));
        current = std::move(limitOp);
    }

    // DISTINCT (after all other operators)
    if (ret.distinct && !has_aggregate) {
        auto distinctOp = std::make_unique<DistinctOp>();
        distinctOp->children.push_back(std::move(current));
        current = std::move(distinctOp);
    }

    return current;
}

// ==================== CREATE ====================

std::variant<LogicalOperator, std::string> LogicalPlanBuilder::buildCreate(cypher::CreateClause& create) {
    if (create.patterns.empty()) {
        return "CREATE with no patterns";
    }

    std::optional<LogicalOperator> lastOp;

    for (auto& part : create.patterns) {
        auto& elem = part.element;
        auto& node = elem.node;
        std::string nodeVar = node.variable.value_or("");

        if (!nodeVar.empty()) {
            ensureVariable(nodeVar);
        }

        auto createNode = std::make_unique<CreateNodeOp>();
        createNode->variable = nodeVar;
        createNode->labels = std::move(node.labels);
        if (node.properties.has_value()) {
            createNode->properties = std::move(node.properties);
        }

        LogicalOperator current = std::move(createNode);

        for (auto& [rel, nextNode] : elem.chain) {
            std::string dstVar = nextNode.variable.value_or("");
            std::string edgeVar = rel.variable.value_or("");

            if (!dstVar.empty()) {
                ensureVariable(dstVar);
            }
            if (!edgeVar.empty()) {
                ensureVariable(edgeVar);
            }

            auto createDst = std::make_unique<CreateNodeOp>();
            createDst->variable = dstVar;
            createDst->labels = std::move(nextNode.labels);
            if (nextNode.properties.has_value()) {
                createDst->properties = std::move(nextNode.properties);
            }
            createDst->children.push_back(std::move(current));
            current = std::move(createDst);

            auto createEdge = std::make_unique<CreateEdgeOp>();
            createEdge->variable = edgeVar;
            createEdge->src_variable = nodeVar;
            createEdge->dst_variable = dstVar;
            createEdge->rel_types = std::move(rel.rel_types);
            createEdge->direction = rel.direction;
            if (rel.properties.has_value()) {
                createEdge->properties = std::move(rel.properties);
            }
            createEdge->children.push_back(std::move(current));
            current = std::move(createEdge);

            nodeVar = dstVar;
        }

        if (lastOp.has_value()) {
            std::visit([&current](auto& op) { op->children.push_back(std::move(current)); }, *lastOp);
        } else {
            lastOp = std::move(current);
        }
    }

    return std::move(*lastOp);
}

// ==================== Filter ====================

std::variant<LogicalOperator, std::string> LogicalPlanBuilder::buildFilter(cypher::Expression predicate,
                                                                           LogicalOperator input) {
    auto filter = std::make_unique<FilterOp>();
    filter->predicate = std::move(predicate);
    filter->children.push_back(std::move(input));
    return filter;
}

// ==================== toString ====================

namespace {

std::string indentStr(int level) {
    return std::string(level * 2, ' ');
}

struct OperatorPrinter {
    std::ostringstream& os;
    int level = 0;

    void operator()(const std::unique_ptr<AllNodeScanOp>& op) {
        os << indentStr(level) << "AllNodeScan(variable=" << op->variable << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<LabelScanOp>& op) {
        os << indentStr(level) << "LabelScan(variable=" << op->variable << ", label=" << op->label << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<ExpandOp>& op) {
        os << indentStr(level) << "Expand(src=" << op->src_variable << ", dst=" << op->dst_variable;
        if (!op->edge_variable.empty())
            os << ", edge=" << op->edge_variable;
        os << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<FilterOp>& op) {
        os << indentStr(level) << "Filter\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<ProjectOp>& op) {
        os << indentStr(level) << "Project(distinct=" << (op->distinct ? "true" : "false") << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<AggregateOp>& op) {
        os << indentStr(level) << "Aggregate(keys=" << op->group_keys.size() << ", aggs=" << op->aggregates.size()
           << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<SortOp>& op) {
        os << indentStr(level) << "Sort(items=" << op->sort_items.size() << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<SkipOp>& op) {
        os << indentStr(level) << "Skip(" << op->skip << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<DistinctOp>& op) {
        os << indentStr(level) << "Distinct\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<LimitOp>& op) {
        os << indentStr(level) << "Limit(" << op->limit << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<CreateNodeOp>& op) {
        os << indentStr(level) << "CreateNode(variable=" << op->variable << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }
    void operator()(const std::unique_ptr<CreateEdgeOp>& op) {
        os << indentStr(level) << "CreateEdge(src=" << op->src_variable << ", dst=" << op->dst_variable << ")\n";
        for (const auto& child : op->children)
            printChild(child);
    }

    void printChild(const LogicalOperator& child) {
        OperatorPrinter childPrinter{os, level + 1};
        std::visit(childPrinter, child);
    }
};

} // anonymous namespace

std::string logicalOperatorToString(const LogicalOperator& op, int indentLevel) {
    std::ostringstream os;
    OperatorPrinter printer{os, indentLevel};
    std::visit(printer, op);
    return os.str();
}

} // namespace compute
} // namespace eugraph
