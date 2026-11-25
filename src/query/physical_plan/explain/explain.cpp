#include "query/physical_plan/explain/explain.hpp"

#include "common/schema/graph_schema.hpp"
#include "query/dataset/row.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

#include <algorithm>
#include <sstream>
#include <type_traits>

namespace eugraph {
namespace compute {
namespace {

const char* binarySymbol(cypher::BinaryOperator op) {
    switch (op) {
    case cypher::BinaryOperator::ADD:
        return "+";
    case cypher::BinaryOperator::SUB:
        return "-";
    case cypher::BinaryOperator::MUL:
        return "*";
    case cypher::BinaryOperator::DIV:
        return "/";
    case cypher::BinaryOperator::MOD:
        return "%";
    case cypher::BinaryOperator::POW:
        return "^";
    case cypher::BinaryOperator::EQ:
        return "=";
    case cypher::BinaryOperator::NEQ:
        return "<>";
    case cypher::BinaryOperator::LT:
        return "<";
    case cypher::BinaryOperator::GT:
        return ">";
    case cypher::BinaryOperator::LTE:
        return "<=";
    case cypher::BinaryOperator::GTE:
        return ">=";
    case cypher::BinaryOperator::STARTS_WITH:
        return "STARTS WITH";
    case cypher::BinaryOperator::ENDS_WITH:
        return "ENDS WITH";
    case cypher::BinaryOperator::CONTAINS:
        return "CONTAINS";
    case cypher::BinaryOperator::IN:
        return "IN";
    case cypher::BinaryOperator::AND:
        return "AND";
    case cypher::BinaryOperator::OR:
        return "OR";
    case cypher::BinaryOperator::XOR:
        return "XOR";
    case cypher::BinaryOperator::LIST_CONCAT:
        return "||";
    }
    return "?";
}

const char* unarySymbol(cypher::UnaryOperator op) {
    switch (op) {
    case cypher::UnaryOperator::NOT:
        return "NOT ";
    case cypher::UnaryOperator::NEGATE:
        return "-";
    case cypher::UnaryOperator::PLUS:
        return "+";
    case cypher::UnaryOperator::IS_NULL:
        return " IS NULL";
    case cypher::UnaryOperator::IS_NOT_NULL:
        return " IS NOT NULL";
    }
    return "?";
}

std::string formatValue(const Value& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream out;
                out << v;
                return out.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "'" + v + "'";
            } else if constexpr (std::is_same_v<T, ListValuePtr>) {
                return "<list>";
            } else if constexpr (std::is_same_v<T, MapValuePtr>) {
                return "<map>";
            } else {
                return "<value>";
            }
        },
        value);
}

void appendList(const std::vector<std::string>& items, std::string& out) {
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += items[i];
    }
}

std::string headline(const ExplainNode& node) {
    std::string out = node.operator_name;
    if (!node.details.empty()) {
        out += "(";
        for (size_t i = 0; i < node.details.size(); ++i) {
            if (i > 0)
                out += ", ";
            out += node.details[i];
        }
        out += ")";
    }
    if (node.estimated_rows.has_value()) {
        out += " [rows≈";
        std::ostringstream rows;
        rows << static_cast<int64_t>(*node.estimated_rows);
        out += rows.str();
        out += "]";
    }
    return out;
}

void renderNode(const ExplainNode& node, const std::string& prefix, bool last, bool root, const ExplainContext& ctx,
                std::vector<std::string>& lines) {
    std::string connector = root ? "" : (last ? "└─ " : "├─ ");
    lines.push_back(prefix + connector + headline(node));
    if (ctx.verbose && !node.identifiers.empty()) {
        std::string indent = prefix + (root ? "   " : (last ? "   " : "│  "));
        std::string out = indent + "outputs: ";
        appendList(node.identifiers, out);
        lines.push_back(std::move(out));
    }
    for (size_t i = 0; i < node.children.size(); ++i) {
        std::string child_prefix = prefix + (root ? "" : (last ? "   " : "│  "));
        renderNode(*node.children[i], child_prefix, i + 1 == node.children.size(), false, ctx, lines);
    }
}

} // namespace

std::string explainLabelName(LabelId id, const ExplainContext& ctx) {
    if (id == INVALID_LABEL_ID && ctx.schema == nullptr)
        return "<unknown-label>";
    if (ctx.schema != nullptr) {
        auto it = ctx.schema->labels.find(id);
        if (it != ctx.schema->labels.end() && !it->second.name.empty())
            return it->second.name;
    }
    if (id == INVALID_LABEL_ID)
        return "<unknown-label>";
    return "<label#" + std::to_string(id) + ">";
}

std::string explainEdgeLabelName(EdgeLabelId id, const ExplainContext& ctx) {
    if (ctx.schema != nullptr) {
        auto it = ctx.schema->edge_labels.find(id);
        if (it != ctx.schema->edge_labels.end() && !it->second.name.empty())
            return it->second.name;
    }
    return "<edge#" + std::to_string(id) + ">";
}

std::string explainPropertyName(LabelId label_id, uint16_t prop_id, const ExplainContext& ctx) {
    if (ctx.schema != nullptr) {
        auto it = ctx.schema->labels.find(label_id);
        if (it != ctx.schema->labels.end()) {
            for (const auto& prop : it->second.properties) {
                if (prop.id == prop_id)
                    return prop.name;
            }
        }
    }
    return "<prop#" + std::to_string(prop_id) + ">";
}

std::string explainLabelList(const std::vector<LabelId>& ids, const ExplainContext& ctx) {
    std::string out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0)
            out += ":";
        out += explainLabelName(ids[i], ctx);
    }
    return out;
}

std::string explainEdgeLabelList(const std::vector<EdgeLabelId>& ids, const ExplainContext& ctx) {
    std::string out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0)
            out += "|";
        out += explainEdgeLabelName(ids[i], ctx);
    }
    return out;
}

std::string explainPropertyList(LabelId label_id, const std::vector<uint16_t>& prop_ids, const ExplainContext& ctx) {
    std::string out;
    for (size_t i = 0; i < prop_ids.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += explainPropertyName(label_id, prop_ids[i], ctx);
    }
    return out;
}

std::string formatExplainPropertyValue(const PropertyValue& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream out;
                out << v;
                return out.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "'" + v + "'";
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                return "<bytes>";
            } else {
                return "<value>";
            }
        },
        value);
}

std::string formatExplainExpression(const binder::BoundExpression& expr, const ExplainContext& ctx) {
    std::string out;
    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                out = val.name;
                if (ctx.slot_semantics != nullptr) {
                    auto it = ctx.slot_semantics->find(val.slot_id);
                    if (it != ctx.slot_semantics->end() && it->second.kind == binder::SlotSemanticKind::Property &&
                        !it->second.property_name.empty())
                        out = it->second.property_name;
                }
                if (ctx.verbose && val.slot_id != 0)
                    out += "#slot" + std::to_string(val.slot_id);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                out = val.name;
            } else if constexpr (std::is_same_v<T, binder::BoundLiteral>) {
                out = formatValue(val.value);
            } else if constexpr (std::is_same_v<T, binder::BoundParameter>) {
                out = "$" + val.name;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (val) {
                    out = formatExplainExpression(val->object, ctx) + "." + val->property_name;
                    if (ctx.verbose && val->candidates.size() == 1)
                        out += " [prop#" + std::to_string(val->candidates.front().prop_id) + "]";
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (val)
                    out = formatExplainExpression(val->object, ctx) + "." + val->property;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (val)
                    out = formatExplainExpression(val->object, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (val) {
                    out = "(" + formatExplainExpression(val->left, ctx) + " " + binarySymbol(val->op) + " " +
                          formatExplainExpression(val->right, ctx) + ")";
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (val) {
                    auto operand = formatExplainExpression(val->operand, ctx);
                    out = std::string(unarySymbol(val->op)) + operand;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (val && val->func_def) {
                    out = val->func_def->name;
                    if (val->distinct)
                        out += " DISTINCT";
                    out += "(";
                    std::vector<std::string> args;
                    args.reserve(val->args.size());
                    for (const auto& arg : val->args)
                        args.push_back(formatExplainExpression(arg, ctx));
                    appendList(args, out);
                    out += ")";
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (val) {
                    out = "[";
                    std::vector<std::string> items;
                    items.reserve(val->elements.size());
                    for (const auto& e : val->elements)
                        items.push_back(formatExplainExpression(e, ctx));
                    appendList(items, out);
                    out += "]";
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                out = "<map>";
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                out = "<case>";
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                out = "<subscript>";
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                out = "<slice>";
            } else {
                out = "<expr#" + std::to_string(expr.index()) + ">";
            }
        },
        expr);
    return out;
}

ExplainNode ExplainBuilder::build(const PhysicalOperator& root, const ExplainContext& ctx) {
    ExplainNode node;
    root.explain(node, ctx);
    if (node.identifiers.empty()) {
        for (const auto& name : root.outputSchema())
            node.identifiers.push_back(name);
    }
    for (const auto* child : root.children()) {
        if (child)
            node.children.push_back(std::make_unique<ExplainNode>(build(*child, ctx)));
    }
    return node;
}

std::string formatExplainTree(const ExplainNode& root, const ExplainContext& ctx) {
    std::vector<std::string> lines;
    if (!root.plan_source.empty())
        lines.push_back("Plan source: " + root.plan_source);
    renderNode(root, "", true, true, ctx, lines);
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0)
            out += "\n";
        out += lines[i];
    }
    return out;
}

} // namespace compute
} // namespace eugraph
