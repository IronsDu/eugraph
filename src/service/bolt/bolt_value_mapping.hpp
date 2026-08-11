#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"
#include "service/bolt/bolt_messages.hpp"

#include <unordered_map>

namespace eugraph {
namespace service {
namespace bolt {

/// Convert internal EuGraph Value to a Bolt PackStream Value suitable for
/// serialization in RECORD messages.
/// Needs label/edge-label definitions for resolving IDs to names.
/// `bolt_version` is the negotiated protocol version (e.g. 0x0501 for v5.1)
/// used to select the correct struct tags for temporal types.
packstream::Value valueToBolt(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                              const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs,
                              uint32_t bolt_version = 0x00000501);

/// Convert a Bolt PackStream dict to internal parameter values (string → Value).
/// This is called when processing RUN message parameters.
/// Bolt parameters come as typed PackStream values, unlike Thrift which passes
/// Cypher literal strings.
Value boltParamToValue(const packstream::Value& v);

} // namespace bolt
} // namespace service
} // namespace eugraph
