#include "query/planner/binder/pattern/pattern_legality_analyzer.hpp"

namespace eugraph {
namespace binder {
namespace pattern {

namespace {
VariableRole roleForOuter(const BoundType& type) {
    switch (type.kind) {
    case BoundTypeKind::VERTEX:
    case BoundTypeKind::VERTEX_REF:
        return VariableRole::Node;
    case BoundTypeKind::EDGE:
    case BoundTypeKind::EDGE_KEY:
        return VariableRole::Relationship;
    case BoundTypeKind::PATH:
    case BoundTypeKind::PATH_TOPOLOGY:
        return VariableRole::Path;
    default:
        return VariableRole::Scalar;
    }
}
} // namespace

PatternScopeAnalysis PatternLegalityAnalyzer::analyze(const MatchPatternGraph& graph,
                                                      const std::unordered_map<std::string, ColumnInfo>& outer) const {
    PatternScopeAnalysis result;
    result.parts.resize(graph.parts.size());

    struct RoleInfo {
        VariableRole role = VariableRole::Scalar;
        bool from_outer = false;
        bool from_current_pattern = false;
    };
    std::unordered_map<std::string, RoleInfo> roles;
    std::unordered_set<std::string> wildcard_outer;

    for (const auto& [name, info] : outer) {
        if (info.type.kind == BoundTypeKind::ANY || info.type.kind == BoundTypeKind::NULL_TYPE) {
            wildcard_outer.insert(name);
            continue;
        }
        RoleInfo ri;
        ri.role = roleForOuter(info.type);
        ri.from_outer = true;
        roles.emplace(name, ri);
    }

    std::unordered_set<std::string> seen_before;

    auto classify = [&](PatternPartScope& scope, const std::string& name) {
        if (outer.count(name) != 0) {
            if (seen_before.count(name) == 0)
                scope.outer_variables.insert(name);
            else
                scope.reused_variables.insert(name);
        } else if (seen_before.count(name) != 0) {
            scope.reused_variables.insert(name);
        } else {
            scope.new_variables.insert(name);
        }
        seen_before.insert(name);
    };

    auto conflictCode = [](const RoleInfo& existing, VariableRole desired) -> std::string {
        if (existing.from_current_pattern && existing.role == VariableRole::Path)
            return "VariableAlreadyBound";
        if (existing.from_outer && existing.role == VariableRole::Scalar && desired == VariableRole::Path)
            return "VariableAlreadyBound";
        return "VariableTypeConflict";
    };

    auto issueMessage = [](const std::string& code, const std::string& name) {
        return code == "VariableAlreadyBound"
                   ? "VariableAlreadyBound: variable '" + name + "' already defined as a value"
                   : "VariableTypeConflict: variable '" + name + "' already defined as ";
    };

    for (const auto& part : graph.parts) {
        auto& scope = result.parts[part.id];

        if (part.path_variable && !part.path_variable->empty()) {
            const std::string& name = *part.path_variable;
            auto existing = roles.find(name);
            if (wildcard_outer.count(name) == 0 && existing != roles.end() &&
                existing->second.role != VariableRole::Path) {
                std::string code = conflictCode(existing->second, VariableRole::Path);
                result.issues.push_back({name, code, issueMessage(code, name)});
            } else {
                RoleInfo ri;
                ri.role = VariableRole::Path;
                ri.from_current_pattern = true;
                roles[name] = ri;
            }
            classify(scope, name);
        }

        for (size_t i = 0; i < part.ordered_elements.size(); ++i) {
            bool is_node = (i % 2 == 0);
            std::string name;
            if (is_node) {
                if (part.ordered_elements[i] < graph.nodes.size())
                    name = graph.nodes[part.ordered_elements[i]].variable;
            } else {
                if (part.ordered_elements[i] < graph.relationships.size())
                    name = graph.relationships[part.ordered_elements[i]].variable;
            }
            if (name.empty())
                continue;

            VariableRole desired = is_node ? VariableRole::Node : VariableRole::Relationship;
            auto existing = roles.find(name);
            if (wildcard_outer.count(name) == 0 && existing != roles.end() && existing->second.role != desired) {
                std::string code = conflictCode(existing->second, desired);
                result.issues.push_back({name, code, issueMessage(code, name)});
            } else {
                RoleInfo ri;
                ri.role = desired;
                ri.from_current_pattern = true;
                roles[name] = ri;
            }
            classify(scope, name);
        }
    }

    for (const auto& [name, info] : roles)
        result.roles[name] = info.role;
    return result;
}

} // namespace pattern
} // namespace binder
} // namespace eugraph
