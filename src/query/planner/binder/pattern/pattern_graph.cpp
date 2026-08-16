#include "query/planner/binder/pattern/pattern_graph.hpp"

#include <algorithm>
#include <string>

namespace eugraph {
namespace binder {

namespace {

std::string stripLeadingColon(std::string name) {
    if (!name.empty() && name.front() == ':')
        name.erase(name.begin());
    return name;
}

int64_t literalInt(const cypher::Expression& expr) {
    if (!std::holds_alternative<std::unique_ptr<cypher::Literal>>(expr))
        return 1;
    const auto& lit = std::get<std::unique_ptr<cypher::Literal>>(expr);
    if (!lit)
        return 1;
    if (std::holds_alternative<int64_t>(lit->value))
        return std::get<int64_t>(lit->value);
    return 1;
}

} // namespace

std::vector<std::string> normalizeRelationshipTypes(const std::vector<std::string>& types) {
    std::vector<std::string> result;
    for (const auto& raw : types) {
        auto type = stripLeadingColon(raw);
        if (type.empty())
            continue;
        if (std::find(result.begin(), result.end(), type) == result.end())
            result.push_back(std::move(type));
    }
    return result;
}

MatchPatternGraph MatchPatternGraphBuilder::build(const cypher::MatchClause& match) const {
    MatchPatternGraph graph;

    for (const auto& pp : match.patterns) {
        PatternPartInfo part;
        part.id = graph.parts.size();
        part.path_variable = pp.variable;

        const auto& element = pp.element;

        // Start node.
        PatternNodeInfo start;
        start.id = graph.nodes.size();
        start.variable = element.node.variable.value_or("");
        start.anonymous = !element.node.variable.has_value();
        start.labels = element.node.labels;
        start.ast_node = &element.node;
        graph.nodes.push_back(start);
        part.ordered_elements.push_back(start.id);

        size_t src_id = start.id;
        for (const auto& [rel_pat, node_pat] : element.chain) {
            PatternRelationshipInfo rel;
            rel.id = graph.relationships.size();
            rel.src_node = src_id;
            rel.variable = rel_pat.variable.value_or("");
            rel.anonymous = !rel_pat.variable.has_value();
            rel.types = normalizeRelationshipTypes(rel_pat.rel_types);
            rel.direction = rel_pat.direction;
            rel.ast_rel = &rel_pat;
            if (rel_pat.range.has_value()) {
                rel.var_length = true;
                rel.min_hops = literalInt(rel_pat.range->first);
                rel.max_hops = literalInt(rel_pat.range->second);
            }
            graph.relationships.push_back(rel);

            PatternNodeInfo node;
            node.id = graph.nodes.size();
            node.variable = node_pat.variable.value_or("");
            node.anonymous = !node_pat.variable.has_value();
            node.labels = node_pat.labels;
            node.ast_node = &node_pat;
            graph.nodes.push_back(node);

            part.ordered_elements.push_back(rel.id);
            part.ordered_elements.push_back(node.id);
            src_id = node.id;
        }

        graph.parts.push_back(std::move(part));
    }

    return graph;
}

} // namespace binder
} // namespace eugraph
