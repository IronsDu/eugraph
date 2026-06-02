#include "query/physical_plan/operator/merge_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"

#include <spdlog/spdlog.h>

namespace {

eugraph::PropertyValue valueToPropertyValue(const eugraph::Value& v) {
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v);
    if (std::holds_alternative<int64_t>(v))
        return std::get<int64_t>(v);
    if (std::holds_alternative<double>(v))
        return std::get<double>(v);
    if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
    if (std::holds_alternative<eugraph::DateTimeValue>(v))
        return std::get<eugraph::DateTimeValue>(v);
    if (std::holds_alternative<eugraph::TimeValue>(v))
        return std::get<eugraph::TimeValue>(v);
    if (std::holds_alternative<eugraph::DurationValue>(v))
        return std::get<eugraph::DurationValue>(v);
    if (std::holds_alternative<eugraph::ListValue>(v)) {
        const auto& lv = std::get<eugraph::ListValue>(v);
        if (lv.elements.empty())
            return eugraph::PropertyValue{};
        const auto& first = lv.elements[0].value;
        if (std::holds_alternative<int64_t>(first)) {
            std::vector<int64_t> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<int64_t>(e.value))
                    arr.push_back(std::get<int64_t>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<double>(first)) {
            std::vector<double> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<double>(e.value))
                    arr.push_back(std::get<double>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<std::string>(first)) {
            std::vector<std::string> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<std::string>(e.value))
                    arr.push_back(std::get<std::string>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        }
    }
    return eugraph::PropertyValue{};
}

eugraph::Value evaluateExpr(eugraph::compute::VectorizedEvaluator& evaluator,
                            const eugraph::binder::BoundExpression& expr, const eugraph::DataChunk* chunk,
                            size_t row_idx) {
    if (chunk && chunk->count > 0) {
        eugraph::DataChunk single_row;
        single_row.count = 1;
        for (size_t c = 0; c < chunk->numColumns(); ++c) {
            eugraph::Column col = eugraph::Column::flat(chunk->columns[c].type, 1);
            col.setValue(0, chunk->getValue(c, row_idx));
            single_row.columns.push_back(std::move(col));
        }
        eugraph::Column result_col = eugraph::Column::flat(eugraph::binder::BoundTypeKind::ANY, 1);
        evaluator.evaluate(expr, single_row, result_col);
        return result_col.getValue(0);
    }
    eugraph::DataChunk empty;
    empty.count = 1;
    eugraph::Column result_col = eugraph::Column::flat(eugraph::binder::BoundTypeKind::ANY, 1);
    evaluator.evaluate(expr, empty, result_col);
    return result_col.getValue(0);
}

eugraph::Direction toStoreDir(eugraph::cypher::RelationshipDirection dir) {
    switch (dir) {
    case eugraph::cypher::RelationshipDirection::LEFT_TO_RIGHT:
        return eugraph::Direction::OUT;
    case eugraph::cypher::RelationshipDirection::RIGHT_TO_LEFT:
        return eugraph::Direction::IN;
    case eugraph::cypher::RelationshipDirection::UNDIRECTED:
        return eugraph::Direction::BOTH;
    }
    return eugraph::Direction::OUT;
}

} // namespace

namespace eugraph {
namespace compute {

MergePhysicalOp::MergePhysicalOp(std::string start_var, bool start_pre_bound, std::vector<LabelId> start_labels,
                                 std::vector<std::pair<uint16_t, binder::BoundExpression>> start_prop_filters,
                                 std::vector<std::pair<std::string, binder::BoundExpression>> start_pending_props,
                                 bool has_relationship, std::string edge_var, std::optional<EdgeLabelId> edge_label_id,
                                 std::optional<std::string> edge_label_name, cypher::RelationshipDirection direction,
                                 std::vector<std::pair<uint16_t, binder::BoundExpression>> edge_prop_filters,
                                 std::vector<std::pair<std::string, binder::BoundExpression>> edge_pending_props,
                                 std::string end_var, bool end_pre_bound, std::vector<LabelId> end_labels,
                                 std::vector<std::pair<uint16_t, binder::BoundExpression>> end_prop_filters,
                                 std::vector<std::pair<std::string, binder::BoundExpression>> end_pending_props,
                                 std::optional<std::string> path_variable,
                                 std::vector<SetPhysicalOp::BoundSetItem> on_create_items,
                                 std::vector<SetPhysicalOp::BoundSetItem> on_match_items, IAsyncGraphDataStore& store,
                                 IAsyncGraphMetaStore& meta, std::unordered_map<LabelId, LabelDef>& label_defs,
                                 const std::unordered_map<std::string, LabelId>& label_name_to_id,
                                 std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs,
                                 const std::unordered_map<std::string, EdgeLabelId>& edge_label_name_to_id,
                                 std::unique_ptr<PhysicalOperator> child)
    : start_var_(std::move(start_var)), start_pre_bound_(start_pre_bound), start_labels_(std::move(start_labels)),
      start_prop_filters_(std::move(start_prop_filters)), start_pending_props_(std::move(start_pending_props)),
      has_relationship_(has_relationship), edge_var_(std::move(edge_var)), edge_label_id_(edge_label_id),
      edge_label_name_(std::move(edge_label_name)), direction_(direction),
      edge_prop_filters_(std::move(edge_prop_filters)), edge_pending_props_(std::move(edge_pending_props)),
      end_var_(std::move(end_var)), end_pre_bound_(end_pre_bound), end_labels_(std::move(end_labels)),
      end_prop_filters_(std::move(end_prop_filters)), end_pending_props_(std::move(end_pending_props)),
      path_variable_(std::move(path_variable)), on_create_items_(std::move(on_create_items)),
      on_match_items_(std::move(on_match_items)), store_(store), meta_(meta), label_defs_(label_defs),
      label_name_to_id_(label_name_to_id), edge_label_defs_(edge_label_defs),
      edge_label_name_to_id_(edge_label_name_to_id), child_(std::move(child)) {
    for (const auto& [lid, def] : label_defs_) {
        if (def.name == kAnonLabelName) {
            anon_label_id_ = lid;
            break;
        }
    }
}

std::string MergePhysicalOp::toString() const {
    if (has_relationship_)
        return "Merge((" + start_var_ + ")-[" + edge_var_ + "]->(" + end_var_ + "))";
    return "Merge(" + start_var_ + ")";
}

std::vector<const PhysicalOperator*> MergePhysicalOp::children() const {
    return {child_.get()};
}

folly::coro::Task<void> MergePhysicalOp::ensureLabelTables(const std::vector<LabelId>& labels) {
    for (auto lid : labels) {
        if (lid == INVALID_LABEL_ID)
            continue;
        auto it = label_defs_.find(lid);
        if (it == label_defs_.end()) {
            auto def = co_await meta_.getLabelDefById(lid);
            if (def)
                label_defs_[lid] = std::move(*def);
        }
        co_await store_.createLabel(lid);
    }
}

folly::coro::Task<void> MergePhysicalOp::ensureEdgeLabelTable(EdgeLabelId elid) {
    if (elid == INVALID_EDGE_LABEL_ID)
        co_return;
    auto it = edge_label_defs_.find(elid);
    if (it == edge_label_defs_.end()) {
        auto def = co_await meta_.getEdgeLabelDefById(elid);
        if (def)
            edge_label_defs_[elid] = std::move(*def);
    }
    co_await store_.createEdgeLabel(elid);
}

folly::coro::Task<void>
MergePhysicalOp::registerPendingProps(const std::vector<std::pair<std::string, binder::BoundExpression>>&,
                                      const std::vector<std::pair<std::string, binder::BoundExpression>>&,
                                      const std::vector<std::pair<std::string, binder::BoundExpression>>&,
                                      const std::vector<std::pair<std::string, binder::BoundExpression>>&) {
    if (anon_label_id_ == INVALID_LABEL_ID) {
        anon_label_id_ = co_await meta_.createLabel(std::string(kAnonLabelName), {});
        if (anon_label_id_ != INVALID_LABEL_ID) {
            co_await store_.createLabel(anon_label_id_);
            auto def = co_await meta_.getLabelDefById(anon_label_id_);
            if (def)
                label_defs_[anon_label_id_] = std::move(*def);
        }
    }
    co_return;
}

bool MergePhysicalOp::comparePropertyValue(const PropertyValue& stored, const Value& expected) {
    return std::visit(
        [&expected](const auto& sv) -> bool {
            using T = std::decay_t<decltype(sv)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return std::holds_alternative<std::monostate>(expected);
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::holds_alternative<bool>(expected) && sv == std::get<bool>(expected);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::holds_alternative<int64_t>(expected) && sv == std::get<int64_t>(expected);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::holds_alternative<double>(expected) && sv == std::get<double>(expected);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::holds_alternative<std::string>(expected) && sv == std::get<std::string>(expected);
            }
            return false;
        },
        stored);
}

folly::coro::Task<std::optional<VertexId>>
MergePhysicalOp::findMatchingNode(const std::vector<LabelId>& labels,
                                  const std::vector<std::pair<uint16_t, binder::BoundExpression>>& prop_filters,
                                  const std::vector<std::pair<std::string, binder::BoundExpression>>& pending_props,
                                  const DataChunk* chunk, size_t row_idx, VectorizedEvaluator& evaluator) {
    // Check created_vertices first
    for (auto vid : created_vertices_) {
        bool match = true;
        for (const auto& [prop_id, expr] : prop_filters) {
            Value expected = evaluateExpr(evaluator, expr, chunk, row_idx);
            // Get properties of created vertex
            bool found_match = false;
            for (auto lid : labels) {
                if (lid == INVALID_LABEL_ID)
                    continue;
                auto props = co_await store_.getVertexProperties(vid, lid);
                if (props && prop_id < props->size() && (*props)[prop_id].has_value()) {
                    if (comparePropertyValue((*props)[prop_id].value(), expected)) {
                        found_match = true;
                        break;
                    }
                }
            }
            if (!found_match) {
                // Also check if expected is null — null match means "don't care"
                if (!std::holds_alternative<std::monostate>(expected)) {
                    match = false;
                    break;
                }
            }
        }
        if (match) {
            co_return vid;
        }
    }

    // If no labels, we can't scan
    if (labels.empty()) {
        co_return std::nullopt;
    }

    // Scan vertices by label(s)
    std::unordered_set<VertexId> candidates;
    bool first_label = true;
    for (auto lid : labels) {
        if (lid == INVALID_LABEL_ID)
            continue;
        auto gen = store_.scanVerticesByLabel(lid);
        std::unordered_set<VertexId> label_verts;
        while (auto batch = co_await gen.next()) {
            for (auto vid : *batch) {
                label_verts.insert(vid);
            }
        }
        if (first_label) {
            candidates = std::move(label_verts);
            first_label = false;
        } else {
            std::unordered_set<VertexId> intersection;
            for (auto vid : label_verts) {
                if (candidates.count(vid))
                    intersection.insert(vid);
            }
            candidates = std::move(intersection);
        }
    }

    if (candidates.empty() && prop_filters.empty() && pending_props.empty()) {
        co_return std::nullopt;
    }
    if (candidates.empty()) {
        co_return std::nullopt;
    }

    // If no property filters, return first candidate
    if (prop_filters.empty() && pending_props.empty()) {
        co_return *candidates.begin();
    }

    // Filter by properties
    for (auto vid : candidates) {
        bool match = true;
        for (const auto& [prop_id, expr] : prop_filters) {
            Value expected = evaluateExpr(evaluator, expr, chunk, row_idx);
            bool found_match = false;
            for (auto lid : labels) {
                if (lid == INVALID_LABEL_ID)
                    continue;
                auto props = co_await store_.getVertexProperties(vid, lid);
                if (props && prop_id < props->size() && (*props)[prop_id].has_value()) {
                    if (comparePropertyValue((*props)[prop_id].value(), expected)) {
                        found_match = true;
                        break;
                    }
                }
            }
            if (!found_match) {
                if (!std::holds_alternative<std::monostate>(expected)) {
                    match = false;
                    break;
                }
            }
        }
        // Check pending (name-based) properties
        if (match && !pending_props.empty()) {
            for (const auto& [prop_name, expr] : pending_props) {
                Value expected = evaluateExpr(evaluator, expr, chunk, row_idx);
                bool found_match = false;
                // Check __anon__ and all labels for property by name
                for (auto lid : labels) {
                    if (lid == INVALID_LABEL_ID)
                        continue;
                    auto def_it = label_defs_.find(lid);
                    if (def_it == label_defs_.end())
                        continue;
                    for (const auto& pd : def_it->second.properties) {
                        if (pd.name == prop_name) {
                            auto props = co_await store_.getVertexProperties(vid, lid);
                            if (props && pd.id < props->size() && (*props)[pd.id].has_value()) {
                                if (comparePropertyValue((*props)[pd.id].value(), expected)) {
                                    found_match = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (found_match)
                        break;
                }
                if (!found_match && anon_label_id_ != INVALID_LABEL_ID) {
                    auto anon_props = co_await store_.getVertexProperties(vid, anon_label_id_);
                    if (anon_props) {
                        auto anon_it = label_defs_.find(anon_label_id_);
                        if (anon_it != label_defs_.end()) {
                            for (const auto& pd : anon_it->second.properties) {
                                if (pd.name == prop_name && pd.id < anon_props->size() &&
                                    (*anon_props)[pd.id].has_value()) {
                                    if (comparePropertyValue((*anon_props)[pd.id].value(), expected)) {
                                        found_match = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                if (!found_match && !std::holds_alternative<std::monostate>(expected)) {
                    match = false;
                    break;
                }
            }
        }
        if (match) {
            co_return vid;
        }
    }
    co_return std::nullopt;
}

folly::coro::Task<VertexId>
MergePhysicalOp::createNode(const std::vector<LabelId>& labels,
                            const std::vector<std::pair<uint16_t, binder::BoundExpression>>& prop_filters,
                            const std::vector<std::pair<std::string, binder::BoundExpression>>& pending_props,
                            const DataChunk* chunk, size_t row_idx, VectorizedEvaluator& evaluator) {
    VertexId vid = co_await meta_.nextVertexId();

    // Build properties per label
    std::vector<std::pair<LabelId, Properties>> label_props;
    for (auto lid : labels) {
        if (lid == INVALID_LABEL_ID)
            continue;
        Properties props;
        for (const auto& [prop_id, expr] : prop_filters) {
            Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
            if (!std::holds_alternative<std::monostate>(v)) {
                if (props.size() <= prop_id)
                    props.resize(prop_id + 1);
                props[prop_id] = valueToPropertyValue(v);
            }
        }
        label_props.emplace_back(lid, std::move(props));
    }

    // Handle pending props under __anon__
    if (!pending_props.empty() && anon_label_id_ != INVALID_LABEL_ID) {
        Properties anon_props;
        for (const auto& [prop_name, expr] : pending_props) {
            Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
            if (!std::holds_alternative<std::monostate>(v)) {
                auto anon_it = label_defs_.find(anon_label_id_);
                uint16_t pid = 0;
                if (anon_it != label_defs_.end()) {
                    for (const auto& pd : anon_it->second.properties) {
                        if (pd.name == prop_name) {
                            pid = pd.id;
                            break;
                        }
                    }
                }
                if (anon_props.size() <= pid)
                    anon_props.resize(pid + 1);
                anon_props[pid] = valueToPropertyValue(v);
            }
        }
        label_props.emplace_back(anon_label_id_, std::move(anon_props));
    }

    co_await store_.insertVertex(vid, label_props);
    created_vertices_.insert(vid);
    co_return vid;
}

folly::coro::Task<std::optional<std::tuple<EdgeId, EdgeLabelId>>>
MergePhysicalOp::findMatchingEdge(VertexId src_vid, VertexId dst_vid,
                                  const std::vector<std::pair<uint16_t, binder::BoundExpression>>& prop_filters,
                                  const DataChunk* chunk, size_t row_idx, VectorizedEvaluator& evaluator) {
    EdgeLabelId elid = edge_label_id_.value_or(INVALID_EDGE_LABEL_ID);

    // Check created_edges set first
    if (elid != INVALID_EDGE_LABEL_ID) {
        MergeEdgeKey key{src_vid, dst_vid, elid};
        auto it = created_edges_.find(key);
        if (it != created_edges_.end()) {
            co_return std::make_tuple(it->second, elid);
        }
    }

    Direction store_dir = toStoreDir(direction_);

    auto scanAndFilter = [&](Direction dir) -> folly::coro::Task<std::optional<std::tuple<EdgeId, EdgeLabelId>>> {
        auto gen = store_.scanEdges(src_vid, dir, edge_label_id_);
        while (auto batch = co_await gen.next()) {
            for (const auto& entry : *batch) {
                if (entry.neighbor_id != dst_vid)
                    continue;

                EdgeLabelId entry_label = entry.edge_label_id;
                EdgeId eid = entry.edge_id;

                if (!prop_filters.empty()) {
                    auto props = co_await store_.getEdgeProperties(entry_label, eid);
                    if (!props)
                        continue;
                    bool match = true;
                    for (const auto& [prop_id, expr] : prop_filters) {
                        Value expected = evaluateExpr(evaluator, expr, chunk, row_idx);
                        if (prop_id < props->size() && (*props)[prop_id].has_value()) {
                            if (!comparePropertyValue((*props)[prop_id].value(), expected)) {
                                match = false;
                                break;
                            }
                        } else if (!std::holds_alternative<std::monostate>(expected)) {
                            match = false;
                            break;
                        }
                    }
                    if (!match)
                        continue;
                }
                co_return std::make_tuple(eid, entry_label);
            }
        }
        co_return std::nullopt;
    };

    auto result = co_await scanAndFilter(store_dir);
    if (result)
        co_return result;

    if (direction_ == cypher::RelationshipDirection::UNDIRECTED) {
        auto rev_result = co_await scanAndFilter(store_dir == Direction::OUT ? Direction::IN : Direction::OUT);
        if (rev_result)
            co_return rev_result;
    }

    co_return std::nullopt;
}

folly::coro::Task<std::tuple<EdgeId, EdgeLabelId>>
MergePhysicalOp::createEdge(VertexId src_vid, VertexId dst_vid,
                            const std::vector<std::pair<uint16_t, binder::BoundExpression>>& prop_filters,
                            const std::vector<std::pair<std::string, binder::BoundExpression>>& pending_props,
                            const DataChunk* chunk, size_t row_idx, VectorizedEvaluator& evaluator) {
    EdgeLabelId effective_elid = edge_label_id_.value_or(INVALID_EDGE_LABEL_ID);
    if (effective_elid == INVALID_EDGE_LABEL_ID && edge_label_name_.has_value()) {
        auto it = edge_label_name_to_id_.find(*edge_label_name_);
        if (it != edge_label_name_to_id_.end())
            effective_elid = it->second;
    }

    EdgeId eid = co_await meta_.nextEdgeId();

    Properties props;
    for (const auto& [prop_id, expr] : prop_filters) {
        Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
        if (!std::holds_alternative<std::monostate>(v)) {
            if (props.size() <= prop_id)
                props.resize(prop_id + 1);
            props[prop_id] = valueToPropertyValue(v);
        }
    }
    for (const auto& [prop_name, expr] : pending_props) {
        Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
        if (!std::holds_alternative<std::monostate>(v)) {
            auto def_it = edge_label_defs_.find(effective_elid);
            if (def_it != edge_label_defs_.end()) {
                for (const auto& pd : def_it->second.properties) {
                    if (pd.name == prop_name) {
                        if (props.size() <= pd.id)
                            props.resize(pd.id + 1);
                        props[pd.id] = valueToPropertyValue(v);
                        break;
                    }
                }
            }
        }
    }

    bool ok = co_await store_.insertEdge(eid, src_vid, dst_vid, effective_elid, 0, props);
    if (!ok) {
        spdlog::warn("MergePhysicalOp: insertEdge failed for eid={} src={} dst={} label={}", eid, src_vid, dst_vid,
                     effective_elid);
    }
    created_edges_[{src_vid, dst_vid, effective_elid}] = eid;
    co_return std::make_tuple(eid, effective_elid);
}

folly::coro::Task<void> MergePhysicalOp::executeSetItems(const std::vector<SetPhysicalOp::BoundSetItem>& items,
                                                         const DataChunk*, size_t, const DataChunk& merged_chunk,
                                                         VectorizedEvaluator& evaluator) {
    for (const auto& item : items) {
        Value val;
        if (item.value) {
            val = evaluateExpr(evaluator, *item.value, &merged_chunk, 0);
        }

        switch (item.kind) {
        case cypher::SetItemKind::SET_PROPERTY: {
            if (!item.value || std::holds_alternative<std::monostate>(val))
                break;
            auto pv = valueToPropertyValue(val);
            // Check if target is an edge variable
            EdgeId target_eid = INVALID_EDGE_ID;
            VertexId target_vid = INVALID_VERTEX_ID;
            LabelIdSet target_labels;
            for (size_t c = 0; c < merged_chunk.numColumns(); ++c) {
                const auto& cv = merged_chunk.getValue(c, 0);
                if (std::holds_alternative<EdgeValue>(cv)) {
                    target_eid = std::get<EdgeValue>(cv).id;
                } else if (std::holds_alternative<VertexValue>(cv)) {
                    const auto& vv = std::get<VertexValue>(cv);
                    target_vid = vv.id;
                    if (vv.labels)
                        target_labels = *vv.labels;
                }
            }
            // Edge property: use putEdgeProperty
            if (target_eid != INVALID_EDGE_ID && item.resolved_prop_id) {
                EdgeLabelId elid = edge_label_id_.value_or(INVALID_EDGE_LABEL_ID);
                co_await store_.putEdgeProperty(target_eid, elid, *item.resolved_prop_id, pv);
                break;
            }
            if (target_vid == INVALID_VERTEX_ID)
                break;

            if (item.strong_mode && item.resolved_prop_id && item.resolved_label_id) {
                co_await store_.putVertexProperty(target_vid, *item.resolved_label_id, *item.resolved_prop_id, pv);
            } else {
                // Convenience mode: resolve label at runtime
                LabelId resolved_lid = INVALID_LABEL_ID;
                uint16_t resolved_pid = 0;
                for (auto lid : target_labels) {
                    if (lid == INVALID_LABEL_ID || lid == anon_label_id_)
                        continue;
                    auto def_it = label_defs_.find(lid);
                    if (def_it != label_defs_.end()) {
                        for (const auto& pd : def_it->second.properties) {
                            if (pd.name == item.prop_name) {
                                resolved_lid = lid;
                                resolved_pid = pd.id;
                                break;
                            }
                        }
                    }
                    if (resolved_lid != INVALID_LABEL_ID)
                        break;
                }
                if (resolved_lid != INVALID_LABEL_ID) {
                    co_await store_.putVertexProperty(target_vid, resolved_lid, resolved_pid, pv);
                } else if (anon_label_id_ != INVALID_LABEL_ID) {
                    // Fallback to __anon__
                    uint16_t pid = 0;
                    auto def_it = label_defs_.find(anon_label_id_);
                    if (def_it != label_defs_.end()) {
                        for (const auto& pd : def_it->second.properties) {
                            if (pd.name == item.prop_name) {
                                pid = pd.id;
                                break;
                            }
                        }
                    }
                    co_await store_.putVertexProperty(target_vid, anon_label_id_, pid, pv);
                }
            }
            break;
        }
        case cypher::SetItemKind::SET_LABELS: {
            if (item.resolved_label_id) {
                VertexId vid = INVALID_VERTEX_ID;
                for (size_t c = 0; c < merged_chunk.numColumns(); ++c) {
                    const auto& cv = merged_chunk.getValue(c, 0);
                    if (std::holds_alternative<VertexValue>(cv)) {
                        vid = std::get<VertexValue>(cv).id;
                        break;
                    }
                }
                if (vid != INVALID_VERTEX_ID)
                    co_await store_.addVertexLabel(vid, *item.resolved_label_id);
            }
            break;
        }
        case cypher::SetItemKind::SET_PROPERTIES: {
            if (!item.value || std::holds_alternative<std::monostate>(val))
                break;
            VertexId vid = INVALID_VERTEX_ID;
            for (size_t c = 0; c < merged_chunk.numColumns(); ++c) {
                const auto& cv = merged_chunk.getValue(c, 0);
                if (std::holds_alternative<VertexValue>(cv)) {
                    vid = std::get<VertexValue>(cv).id;
                    break;
                }
            }
            if (vid != INVALID_VERTEX_ID && std::holds_alternative<MapValue>(val)) {
                const auto& mv = std::get<MapValue>(val);
                for (const auto& [k, vs] : mv.entries) {
                    auto pv = valueToPropertyValue(vs.value);
                    if (anon_label_id_ != INVALID_LABEL_ID) {
                        uint16_t pid = 0;
                        auto def_it = label_defs_.find(anon_label_id_);
                        if (def_it != label_defs_.end()) {
                            for (const auto& pd : def_it->second.properties) {
                                if (pd.name == k) {
                                    pid = pd.id;
                                    break;
                                }
                            }
                        }
                        co_await store_.putVertexProperty(vid, anon_label_id_, pid, pv);
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

folly::coro::AsyncGenerator<DataChunk> MergePhysicalOp::executeChunk() {
    VectorizedEvaluator evaluator(eval_ctx_);

    // Phase 1: ensure label tables exist
    co_await ensureLabelTables(start_labels_);
    if (has_relationship_) {
        co_await ensureLabelTables(end_labels_);
        // Resolve edge label by name if not already resolved by id
        if (!edge_label_id_.has_value() && edge_label_name_.has_value()) {
            auto it = edge_label_name_to_id_.find(*edge_label_name_);
            if (it != edge_label_name_to_id_.end())
                edge_label_id_ = it->second;
        }
        if (edge_label_id_.has_value())
            co_await ensureEdgeLabelTable(*edge_label_id_);
    }

    // Phase 2: register pending properties with __anon__
    co_await registerPendingProps(start_pending_props_, start_pending_props_, end_pending_props_, edge_pending_props_);

    // Phase 3: per-row processing — yield one chunk per row
    auto child_gen = child_->executeChunk();
    while (auto chunk = co_await child_gen.next()) {
        for (size_t row = 0; row < chunk->count; ++row) {
            bool start_created = false;
            bool end_created = false;
            bool edge_created = false;

            // Step 1: Find or create start node
            VertexId start_vid = INVALID_VERTEX_ID;
            std::optional<VertexValue> start_pre_vv;
            if (start_pre_bound_) {
                for (size_t c = 0; c < chunk->numColumns(); ++c) {
                    const auto& val = chunk->getValue(c, row);
                    if (std::holds_alternative<VertexValue>(val)) {
                        start_pre_vv = std::get<VertexValue>(val);
                        start_vid = start_pre_vv->id;
                        break;
                    }
                }
            } else {
                auto found = co_await findMatchingNode(start_labels_, start_prop_filters_, start_pending_props_,
                                                       &*chunk, row, evaluator);
                if (found) {
                    start_vid = *found;
                } else {
                    start_vid = co_await createNode(start_labels_, start_prop_filters_, start_pending_props_, &*chunk,
                                                    row, evaluator);
                    start_created = true;
                }
            }

            // Step 2: Find or create end node and edge
            VertexId end_vid = INVALID_VERTEX_ID;
            std::optional<VertexValue> end_pre_vv;
            EdgeId edge_id = INVALID_EDGE_ID;
            EdgeLabelId edge_label = INVALID_EDGE_LABEL_ID;

            if (has_relationship_) {
                if (end_pre_bound_) {
                    for (size_t c = 0; c < chunk->numColumns(); ++c) {
                        const auto& val = chunk->getValue(c, row);
                        if (std::holds_alternative<VertexValue>(val)) {
                            VertexValue vv = std::get<VertexValue>(val);
                            if (vv.id != start_vid) {
                                end_pre_vv = vv;
                                end_vid = vv.id;
                                break;
                            }
                        }
                    }
                } else {
                    auto found = co_await findMatchingNode(end_labels_, end_prop_filters_, end_pending_props_, &*chunk,
                                                           row, evaluator);
                    if (found) {
                        end_vid = *found;
                    } else {
                        end_vid = co_await createNode(end_labels_, end_prop_filters_, end_pending_props_, &*chunk, row,
                                                      evaluator);
                        end_created = true;
                    }
                }

                auto found_edge =
                    co_await findMatchingEdge(start_vid, end_vid, edge_prop_filters_, &*chunk, row, evaluator);
                if (found_edge) {
                    std::tie(edge_id, edge_label) = *found_edge;
                } else {
                    std::tie(edge_id, edge_label) = co_await createEdge(start_vid, end_vid, edge_prop_filters_,
                                                                        edge_pending_props_, &*chunk, row, evaluator);
                    edge_created = true;
                }
            }

            // Step 3: Execute ON CREATE/MATCH SET items
            // Build a single-row merged chunk with both child + MERGE columns for SET evaluation
            DataChunk merged;
            merged.count = 1;
            for (size_t c = 0; c < chunk->numColumns(); ++c) {
                Column col = Column::flat(chunk->columns[c].type, 1);
                col.setValue(0, chunk->getValue(c, row));
                merged.columns.push_back(std::move(col));
            }
            if (!start_pre_bound_) {
                Column col = Column::flat(binder::BoundTypeKind::VERTEX, 1);
                VertexValue vv;
                vv.id = start_vid;
                vv.labels = LabelIdSet(start_labels_.begin(), start_labels_.end());
                col.setValue(0, Value(vv));
                merged.columns.push_back(std::move(col));
            }
            if (has_relationship_) {
                if (!end_pre_bound_) {
                    Column col = Column::flat(binder::BoundTypeKind::VERTEX, 1);
                    VertexValue vv;
                    vv.id = end_vid;
                    vv.labels = LabelIdSet(end_labels_.begin(), end_labels_.end());
                    col.setValue(0, Value(vv));
                    merged.columns.push_back(std::move(col));
                }
                {
                    Column col = Column::flat(binder::BoundTypeKind::EDGE, 1);
                    EdgeValue ev;
                    ev.id = edge_id;
                    ev.src_id = start_vid;
                    ev.dst_id = end_vid;
                    ev.label_id = edge_label;
                    col.setValue(0, Value(ev));
                    merged.columns.push_back(std::move(col));
                }
            }

            bool any_created = start_created || end_created || edge_created;
            if (any_created && !on_create_items_.empty()) {
                co_await executeSetItems(on_create_items_, &*chunk, row, merged, evaluator);
            } else if (!any_created && !on_match_items_.empty()) {
                co_await executeSetItems(on_match_items_, &*chunk, row, merged, evaluator);
            }

            // Step 4: Fetch properties and build output chunk (one row)
            // Must fetch AFTER SET items so ON CREATE/MATCH modifications are visible.
            auto buildVertexValue = [&](VertexId vid,
                                         const std::vector<LabelId>& labels) -> folly::coro::Task<VertexValue> {
                VertexValue vv;
                vv.id = vid;
                vv.labels = LabelIdSet(labels.begin(), labels.end());
                for (auto lid : labels) {
                    if (lid == INVALID_LABEL_ID)
                        continue;
                    auto props = co_await store_.getVertexProperties(vid, lid);
                    if (props)
                        vv.properties[lid] = std::move(*props);
                }
                if (anon_label_id_ != INVALID_LABEL_ID) {
                    auto anon_props = co_await store_.getVertexProperties(vid, anon_label_id_);
                    if (anon_props)
                        vv.properties[anon_label_id_] = std::move(*anon_props);
                }
                co_return vv;
            };

            VertexValue start_vv;
            if (start_pre_bound_ && start_pre_vv)
                start_vv = *start_pre_vv;
            else if (!start_pre_bound_)
                start_vv = co_await buildVertexValue(start_vid, start_labels_);
            VertexValue end_vv;
            if (has_relationship_) {
                if (end_pre_bound_ && end_pre_vv)
                    end_vv = *end_pre_vv;
                else if (!end_pre_bound_)
                    end_vv = co_await buildVertexValue(end_vid, end_labels_);
            }

            DataChunk output;
            output.count = 1;
            for (size_t c = 0; c < chunk->numColumns(); ++c) {
                Column col = Column::flat(chunk->columns[c].type, 1);
                col.setValue(0, chunk->getValue(c, row));
                output.columns.push_back(std::move(col));
            }
            if (!start_pre_bound_) {
                Column col = Column::flat(binder::BoundTypeKind::VERTEX, 1);
                col.setValue(0, Value(start_vv));
                output.columns.push_back(std::move(col));
            }
            if (has_relationship_) {
                if (!end_pre_bound_) {
                    Column col = Column::flat(binder::BoundTypeKind::VERTEX, 1);
                    col.setValue(0, Value(end_vv));
                    output.columns.push_back(std::move(col));
                }
                {
                    Column col = Column::flat(binder::BoundTypeKind::EDGE, 1);
                    EdgeValue ev;
                    ev.id = edge_id;
                    ev.src_id = start_vid;
                    ev.dst_id = end_vid;
                    ev.label_id = edge_label;
                    col.setValue(0, Value(ev));
                    output.columns.push_back(std::move(col));
                }
                if (path_variable_.has_value()) {
                    Column col = Column::flat(binder::BoundTypeKind::PATH, 1);
                    PathValue pv;
                    ValueStorage start_vs{Value(start_vv)};
                    ValueStorage edge_vs{Value(EdgeValue{edge_id, start_vid, end_vid, edge_label, 0, std::nullopt})};
                    ValueStorage end_vs{Value(end_vv)};
                    pv.elements = {start_vs, edge_vs, end_vs};
                    col.setValue(0, Value(pv));
                    output.columns.push_back(std::move(col));
                }
            }
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
