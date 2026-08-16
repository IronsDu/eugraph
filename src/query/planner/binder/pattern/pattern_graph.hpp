#pragma once

#include "query/parser/ast.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

/// Pattern-graph IR used by MATCH / OPTIONAL MATCH / EXISTS.
///
/// It intentionally mirrors the AST shape (nodes + directed relationships +
/// named path order) but gives the binder a stable structure to validate and
/// plan without walking the AST repeatedly.
struct PatternNodeInfo {
    size_t id = 0;
    std::string variable; // anonymous variables are always materialised
    bool anonymous = true;
    bool bound = false; // already in scope before this MATCH
    std::vector<std::string> labels;
    const cypher::NodePattern* ast_node = nullptr;
};

struct PatternRelationshipInfo {
    size_t id = 0;
    size_t src_node = 0;
    size_t dst_node = 0;
    std::string variable;
    bool anonymous = true;
    bool bound = false; // already in scope before this MATCH
    bool var_length = false;
    int64_t min_hops = 1;
    int64_t max_hops = 1;
    std::vector<std::string> types; // normalised: no leading ':', deduplicated
    cypher::RelationshipDirection direction = cypher::RelationshipDirection::LEFT_TO_RIGHT;
    const cypher::RelationshipPattern* ast_rel = nullptr;
};

struct PatternPartInfo {
    size_t id = 0;
    std::optional<std::string> path_variable;
    std::vector<size_t> ordered_elements; // alternating node/rel ids, AST order
};

struct MatchPatternGraph {
    std::vector<PatternNodeInfo> nodes;
    std::vector<PatternRelationshipInfo> relationships;
    std::vector<PatternPartInfo> parts;
};

/// Build a MatchPatternGraph from one MATCH / OPTIONAL MATCH clause.
class MatchPatternGraphBuilder {
public:
    MatchPatternGraph build(const cypher::MatchClause& match) const;
};

/// Normalise relationship type names coming from the parser. The grammar
/// accepts both `[:T1|T2]` and `[:T1|:T2]`; the second form can produce a
/// leading colon on alternatives.
std::vector<std::string> normalizeRelationshipTypes(const std::vector<std::string>& types);

} // namespace binder
} // namespace eugraph
