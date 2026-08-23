#pragma once

#include "query/planner/bind_context.hpp"
#include "query/planner/binder/pattern/pattern_graph.hpp"
#include "query/planner/bound_type.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eugraph {
namespace binder {
namespace pattern {

enum class VariableRole {
    Node,
    Relationship,
    Path,
    Scalar,
};

struct PatternScopeIssue {
    std::string variable;
    std::string error_code; // VariableTypeConflict / VariableAlreadyBound
    std::string message;
};

struct PatternPartScope {
    std::unordered_set<std::string> new_variables;
    std::unordered_set<std::string> outer_variables;
    std::unordered_set<std::string> reused_variables;
};

struct PatternScopeAnalysis {
    std::unordered_map<std::string, VariableRole> roles;
    std::vector<PatternPartScope> parts;
    std::vector<PatternScopeIssue> issues;
};

/// Static legality + variable classification over a MatchPatternGraph.
/// Does not bind expressions or allocate slots.
class PatternLegalityAnalyzer {
public:
    PatternScopeAnalysis analyze(const MatchPatternGraph& graph,
                                 const std::unordered_map<std::string, ColumnInfo>& outer) const;
};

} // namespace pattern
} // namespace binder
} // namespace eugraph
