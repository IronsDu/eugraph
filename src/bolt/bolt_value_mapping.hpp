#pragma once

#include "bolt/bolt_messages.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

#include <unordered_map>

namespace eugraph {
namespace bolt {

/// Convert internal EuGraph Value to a Bolt PackStream Value suitable for
/// serialization in RECORD messages.
/// Needs label/edge-label definitions for resolving IDs to names.
packstream::Value valueToBolt(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                              const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs);

/// Convert a Bolt PackStream dict to internal parameter values (string → Value).
/// This is called when processing RUN message parameters.
/// Bolt parameters come as typed PackStream values, unlike Thrift which passes
/// Cypher literal strings.
Value boltParamToValue(const packstream::Value& v);

} // namespace bolt
} // namespace eugraph
