#include "query/physical_plan/operator/merge_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/function/scalar/graph_functions.hpp"

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

// Convert a VertexValue / EdgeValue to a MapValue keyed by property name.
// Used by SET_PROPERTIES when the source expression is an entity reference
// (e.g. `SET r = a` where a is a node) — Cypher semantics: copy all
// properties from the source entity. Returns empty map for non-entity
// inputs (caller falls through to the existing MapValue path).
eugraph::MapValue entityValueToMap(const eugraph::Value& v,
                                   const std::unordered_map<eugraph::LabelId, eugraph::LabelDef>& label_defs,
                                   const std::unordered_map<eugraph::EdgeLabelId, eugraph::EdgeLabelDef>& edge_defs) {
    eugraph::MapValue mv;
    if (std::holds_alternative<eugraph::VertexValue>(v)) {
        const auto& vv = std::get<eugraph::VertexValue>(v);
        for (const auto& [lid, props] : vv.properties) {
            auto it = label_defs.find(lid);
            if (it == label_defs.end())
                continue;
            for (const auto& pd : it->second.properties) {
                if (pd.id < props.size() && props[pd.id].has_value())
                    mv.entries.push_back(
                        {pd.name,
                         eugraph::ValueStorage{eugraph::function::scalar::propertyValueToRuntimeValue(*props[pd.id])}});
            }
        }
    } else if (std::holds_alternative<eugraph::EdgeValue>(v)) {
        const auto& ev = std::get<eugraph::EdgeValue>(v);
        if (!ev.properties.has_value())
            return mv;
        auto it = edge_defs.find(ev.label_id);
        if (it == edge_defs.end())
            return mv;
        const auto& props = *ev.properties;
        for (const auto& pd : it->second.properties) {
            if (pd.id < props.size() && props[pd.id].has_value())
                mv.entries.push_back(
                    {pd.name,
                     eugraph::ValueStorage{eugraph::function::scalar::propertyValueToRuntimeValue(*props[pd.id])}});
        }
    }
    return mv;
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
MergePhysicalOp::registerPendingProps(const std::vector<std::pair<std::string, binder::BoundExpression>>& start_pending,
                                      const std::vector<std::pair<std::string, binder::BoundExpression>>& end_pending,
                                      const std::vector<std::pair<std::string, binder::BoundExpression>>& edge_pending,
                                      const std::vector<SetPhysicalOp::BoundSetItem>& on_create_items,
                                      const std::vector<SetPhysicalOp::BoundSetItem>& on_match_items) {
    if (anon_label_id_ == INVALID_LABEL_ID) {
        anon_label_id_ = co_await meta_.createLabel(std::string(kAnonLabelName), {});
        if (anon_label_id_ != INVALID_LABEL_ID) {
            co_await store_.createLabel(anon_label_id_);
            auto def = co_await meta_.getLabelDefById(anon_label_id_);
            if (def)
                label_defs_[anon_label_id_] = std::move(*def);
        }
    }
    if (anon_label_id_ == INVALID_LABEL_ID)
        co_return;

    // Collect all property names that need registration with __anon__
    std::unordered_set<std::string> prop_names;
    for (const auto& [name, _] : start_pending)
        prop_names.insert(name);
    for (const auto& [name, _] : end_pending)
        prop_names.insert(name);
    for (const auto& [name, _] : edge_pending)
        prop_names.insert(name);

    auto collectSetItemProps = [&](const std::vector<SetPhysicalOp::BoundSetItem>& items) {
        for (const auto& item : items) {
            if (item.kind == cypher::SetItemKind::SET_PROPERTY && !item.prop_name.empty())
                prop_names.insert(item.prop_name);
        }
    };
    collectSetItemProps(on_create_items);
    collectSetItemProps(on_match_items);

    // Register each property with __anon__ label
    for (const auto& name : prop_names) {
        co_await meta_.getOrCreateAnonPropId(name, PropertyType::ANY);
    }

    // Refresh the cached label definition
    auto updated_def = co_await meta_.getLabelDefById(anon_label_id_);
    if (updated_def)
        label_defs_[anon_label_id_] = std::move(*updated_def);

    // Register edge properties (inline pattern + ON CREATE/MATCH SET) with the edge label
    if (has_relationship_ && edge_label_name_.has_value()) {
        std::vector<std::pair<std::string, PropertyType>> edge_props_to_register;
        // Inline MERGE pattern properties (e.g., MERGE (a)-[r:T {name: 'Lola'}]->(b))
        for (const auto& [pname, _] : edge_pending)
            edge_props_to_register.push_back({pname, PropertyType::ANY});
        // ON CREATE / ON MATCH SET edge properties
        auto collectEdgeProps = [&](const std::vector<SetPhysicalOp::BoundSetItem>& items) {
            for (const auto& item : items) {
                if (item.var_name == edge_var_ && !item.prop_name.empty())
                    edge_props_to_register.push_back({item.prop_name, PropertyType::ANY});
            }
        };
        collectEdgeProps(on_create_items);
        collectEdgeProps(on_match_items);
        if (!edge_props_to_register.empty()) {
            co_await meta_.addEdgeLabelProperties(*edge_label_name_, edge_props_to_register);
            // Refresh edge label definition cache
            if (edge_label_id_.has_value()) {
                auto edge_def = co_await meta_.getEdgeLabelDefById(*edge_label_id_);
                if (edge_def)
                    edge_label_defs_[*edge_label_id_] = std::move(*edge_def);
            }
        }
    }
}

bool MergePhysicalOp::comparePropertyValue(const PropertyValue& stored, const Value& expected) {
    return std::visit(
        [&expected](const auto& sv) -> bool {
            using T = std::decay_t<decltype(sv)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return isNull(expected);
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::holds_alternative<bool>(expected) && sv == std::get<bool>(expected);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                if (std::holds_alternative<int64_t>(expected))
                    return sv == std::get<int64_t>(expected);
                if (std::holds_alternative<double>(expected))
                    return static_cast<double>(sv) == std::get<double>(expected);
                return false;
            } else if constexpr (std::is_same_v<T, double>) {
                if (std::holds_alternative<double>(expected))
                    return sv == std::get<double>(expected);
                if (std::holds_alternative<int64_t>(expected))
                    return sv == static_cast<double>(std::get<int64_t>(expected));
                return false;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::holds_alternative<std::string>(expected) && sv == std::get<std::string>(expected);
            } else if constexpr (std::is_same_v<T, DateTimeValue>) {
                return std::holds_alternative<DateTimeValue>(expected) && sv == std::get<DateTimeValue>(expected);
            } else if constexpr (std::is_same_v<T, TimeValue>) {
                return std::holds_alternative<TimeValue>(expected) && sv == std::get<TimeValue>(expected);
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                return std::holds_alternative<DurationValue>(expected) && sv == std::get<DurationValue>(expected);
            } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                if (!std::holds_alternative<ListValue>(expected))
                    return false;
                const auto& lv = std::get<ListValue>(expected);
                if (sv.size() != lv.elements.size())
                    return false;
                for (size_t i = 0; i < sv.size(); ++i) {
                    if (!std::holds_alternative<int64_t>(lv.elements[i].value) ||
                        sv[i] != std::get<int64_t>(lv.elements[i].value))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                if (!std::holds_alternative<ListValue>(expected))
                    return false;
                const auto& lv = std::get<ListValue>(expected);
                if (sv.size() != lv.elements.size())
                    return false;
                for (size_t i = 0; i < sv.size(); ++i) {
                    if (!std::holds_alternative<double>(lv.elements[i].value) ||
                        sv[i] != std::get<double>(lv.elements[i].value))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                if (!std::holds_alternative<ListValue>(expected))
                    return false;
                const auto& lv = std::get<ListValue>(expected);
                if (sv.size() != lv.elements.size())
                    return false;
                for (size_t i = 0; i < sv.size(); ++i) {
                    if (!std::holds_alternative<std::string>(lv.elements[i].value) ||
                        sv[i] != std::get<std::string>(lv.elements[i].value))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::vector<DateTimeValue>>) {
                if (!std::holds_alternative<ListValue>(expected))
                    return false;
                const auto& lv = std::get<ListValue>(expected);
                if (sv.size() != lv.elements.size())
                    return false;
                for (size_t i = 0; i < sv.size(); ++i) {
                    if (!std::holds_alternative<DateTimeValue>(lv.elements[i].value) ||
                        sv[i] != std::get<DateTimeValue>(lv.elements[i].value))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::vector<TimeValue>>) {
                if (!std::holds_alternative<ListValue>(expected))
                    return false;
                const auto& lv = std::get<ListValue>(expected);
                if (sv.size() != lv.elements.size())
                    return false;
                for (size_t i = 0; i < sv.size(); ++i) {
                    if (!std::holds_alternative<TimeValue>(lv.elements[i].value) ||
                        sv[i] != std::get<TimeValue>(lv.elements[i].value))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::vector<DurationValue>>) {
                if (!std::holds_alternative<ListValue>(expected))
                    return false;
                const auto& lv = std::get<ListValue>(expected);
                if (sv.size() != lv.elements.size())
                    return false;
                for (size_t i = 0; i < sv.size(); ++i) {
                    if (!std::holds_alternative<DurationValue>(lv.elements[i].value) ||
                        sv[i] != std::get<DurationValue>(lv.elements[i].value))
                        return false;
                }
                return true;
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

    // If no labels, scan __anon__ table for anonymous nodes
    if (labels.empty()) {
        if (anon_label_id_ == INVALID_LABEL_ID)
            co_return std::nullopt;
        auto gen = store_.scanVerticesByLabel(anon_label_id_);
        while (auto batch = co_await gen.next()) {
            for (auto vid : *batch) {
                if (prop_filters.empty() && pending_props.empty())
                    co_return vid;
                // Check created_vertices set to avoid double-counting
                if (created_vertices_.count(vid))
                    continue;
                // For anonymous nodes with properties, need full scan + filter
                bool match = true;
                for (const auto& [prop_id, expr] : prop_filters) {
                    Value expected = evaluateExpr(evaluator, expr, chunk, row_idx);
                    auto props = co_await store_.getVertexProperties(vid, anon_label_id_);
                    if (props && prop_id < props->size() && (*props)[prop_id].has_value()) {
                        if (!comparePropertyValue((*props)[prop_id].value(), expected))
                            match = false;
                    } else if (!std::holds_alternative<std::monostate>(expected)) {
                        match = false;
                    }
                    if (!match)
                        break;
                }
                // Check pending (name-based) properties
                if (match && !pending_props.empty()) {
                    for (const auto& [prop_name, expr] : pending_props) {
                        Value expected = evaluateExpr(evaluator, expr, chunk, row_idx);
                        bool found_match = false;
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
                        if (!found_match && !std::holds_alternative<std::monostate>(expected)) {
                            match = false;
                            break;
                        }
                    }
                }
                if (match)
                    co_return vid;
            }
        }
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

    // Ensure the node is stored in at least one table (for nodes without labels or properties)
    if (label_props.empty() && anon_label_id_ != INVALID_LABEL_ID) {
        label_props.emplace_back(anon_label_id_, Properties{});
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
                                                         const DataChunk& merged_chunk, VectorizedEvaluator& evaluator,
                                                         VertexId start_vid, VertexId end_vid, EdgeId edge_id) {
    for (const auto& item : items) {
        Value val;
        if (item.value) {
            val = evaluateExpr(evaluator, *item.value, &merged_chunk, 0);
        }
        switch (item.kind) {
        case cypher::SetItemKind::SET_PROPERTY:
            co_await executeSetPropertyItem(item, val, start_vid, end_vid, edge_id);
            break;
        case cypher::SetItemKind::SET_LABELS:
            co_await executeSetLabelsItem(item, start_vid, end_vid);
            break;
        case cypher::SetItemKind::SET_PROPERTIES:
            co_await executeSetPropertiesItem(item, val, start_vid, end_vid, edge_id);
            break;
        default:
            break;
        }
    }
}

folly::coro::Task<void> MergePhysicalOp::executeSetPropertyItem(const SetPhysicalOp::BoundSetItem& item,
                                                                const Value& val, VertexId start_vid, VertexId end_vid,
                                                                EdgeId edge_id) {
    if (!item.value || std::holds_alternative<std::monostate>(val))
        co_return;
    // Edge property
    if (has_relationship_ && item.var_name == edge_var_ && edge_id != INVALID_EDGE_ID) {
        EdgeLabelId elid = edge_label_id_.value_or(INVALID_EDGE_LABEL_ID);
        if (item.resolved_prop_id) {
            co_await store_.putEdgeProperty(edge_id, elid, *item.resolved_prop_id, valueToPropertyValue(val));
        } else {
            uint16_t pid = 0;
            bool found = false;
            auto it = edge_label_defs_.find(elid);
            if (it != edge_label_defs_.end()) {
                for (const auto& pd : it->second.properties) {
                    if (pd.name == item.prop_name) {
                        pid = pd.id;
                        found = true;
                        break;
                    }
                }
            }
            if (!found && edge_label_name_.has_value()) {
                std::vector<std::pair<std::string, PropertyType>> tmp_props{{item.prop_name, PropertyType::ANY}};
                co_await meta_.addEdgeLabelProperties(*edge_label_name_, tmp_props);
                auto updated = co_await meta_.getEdgeLabelDefById(elid);
                if (updated) {
                    edge_label_defs_[elid] = std::move(*updated);
                    for (const auto& pd : edge_label_defs_[elid].properties) {
                        if (pd.name == item.prop_name) {
                            pid = pd.id;
                            break;
                        }
                    }
                }
            }
            co_await store_.putEdgeProperty(edge_id, elid, pid, valueToPropertyValue(val));
        }
        co_return;
    }
    // Vertex property
    VertexId target_vid = INVALID_VERTEX_ID;
    if (item.var_name == start_var_)
        target_vid = start_vid;
    else if (has_relationship_ && item.var_name == end_var_)
        target_vid = end_vid;
    if (target_vid == INVALID_VERTEX_ID)
        co_return;

    if (item.strong_mode && item.resolved_prop_id && item.resolved_label_id) {
        co_await store_.putVertexProperty(target_vid, *item.resolved_label_id, *item.resolved_prop_id,
                                          valueToPropertyValue(val));
        co_return;
    }

    auto* search_labels = (item.var_name == start_var_) ? &start_labels_ : &end_labels_;
    LabelId resolved_lid = INVALID_LABEL_ID;
    uint16_t resolved_pid = 0;
    for (auto lid : *search_labels) {
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
        co_await store_.putVertexProperty(target_vid, resolved_lid, resolved_pid, valueToPropertyValue(val));
    } else {
        // Property not declared on any concrete label of this vertex. Pick
        // the first concrete label and dynamically register the property so
        // the value is persisted under a label the vertex actually owns —
        // otherwise the read path (which iterates vertex labels) won't see
        // it. Falls back to __anon__ only if no concrete label exists.
        LabelId fallback_lid = anon_label_id_;
        for (auto lid : *search_labels) {
            if (lid != INVALID_LABEL_ID && lid != anon_label_id_) {
                fallback_lid = lid;
                break;
            }
        }
        if (fallback_lid != INVALID_LABEL_ID) {
            std::string label_name;
            auto def_it = label_defs_.find(fallback_lid);
            if (def_it != label_defs_.end())
                label_name = def_it->second.name;
            if (label_name.empty()) {
                for (const auto& [name, id] : label_name_to_id_) {
                    if (id == fallback_lid) {
                        label_name = name;
                        break;
                    }
                }
            }
            uint16_t pid = 0;
            bool found = false;
            if (!label_name.empty()) {
                co_await meta_.addVertexLabelProperties(label_name, {{item.prop_name, PropertyType::ANY}});
                auto updated = co_await meta_.getLabelDefById(fallback_lid);
                if (updated) {
                    label_defs_[fallback_lid] = std::move(*updated);
                    for (const auto& pd : label_defs_[fallback_lid].properties) {
                        if (pd.name == item.prop_name) {
                            pid = pd.id;
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (!found && fallback_lid != anon_label_id_) {
                // Could not register (e.g., missing label name); fall back to
                // __anon__ so the SET at least persists somewhere.
                fallback_lid = anon_label_id_;
                pid = 0;
                auto adef_it = label_defs_.find(anon_label_id_);
                if (adef_it != label_defs_.end()) {
                    for (const auto& pd : adef_it->second.properties) {
                        if (pd.name == item.prop_name) {
                            pid = pd.id;
                            break;
                        }
                    }
                }
            }
            co_await store_.putVertexProperty(target_vid, fallback_lid, pid, valueToPropertyValue(val));
        }
    }
}

folly::coro::Task<void> MergePhysicalOp::executeSetLabelsItem(const SetPhysicalOp::BoundSetItem& item,
                                                              VertexId start_vid, VertexId end_vid) {
    if (!item.resolved_label_id)
        co_return;
    VertexId vid = INVALID_VERTEX_ID;
    if (item.var_name == start_var_)
        vid = start_vid;
    else if (has_relationship_ && item.var_name == end_var_)
        vid = end_vid;
    if (vid != INVALID_VERTEX_ID)
        co_await store_.addVertexLabel(vid, *item.resolved_label_id);
}

folly::coro::Task<void> MergePhysicalOp::executeSetPropertiesItem(const SetPhysicalOp::BoundSetItem& item,
                                                                  const Value& val, VertexId start_vid,
                                                                  VertexId end_vid, EdgeId edge_id) {
    if (!item.value || std::holds_alternative<std::monostate>(val))
        co_return;
    // Cypher semantics: SET r = a (where a is an entity) copies all of a's
    // properties to r. The evaluator returns a VertexValue / EdgeValue for
    // entity sources; normalise to MapValue so the existing map-writer path
    // handles both cases.
    Value normalised;
    if (std::holds_alternative<VertexValue>(val) || std::holds_alternative<EdgeValue>(val)) {
        normalised = Value(entityValueToMap(val, label_defs_, edge_label_defs_));
    } else {
        normalised = val;
    }
    // Edge target
    if (has_relationship_ && item.var_name == edge_var_ && edge_id != INVALID_EDGE_ID &&
        std::holds_alternative<MapValue>(normalised)) {
        EdgeLabelId elid = edge_label_id_.value_or(INVALID_EDGE_LABEL_ID);
        const auto& mv = std::get<MapValue>(normalised);
        for (const auto& [k, vs] : mv.entries) {
            uint16_t pid = 0;
            bool found = false;
            auto it = edge_label_defs_.find(elid);
            if (it != edge_label_defs_.end()) {
                for (const auto& pd : it->second.properties) {
                    if (pd.name == k) {
                        pid = pd.id;
                        found = true;
                        break;
                    }
                }
            }
            if (!found && edge_label_name_.has_value()) {
                std::vector<std::pair<std::string, PropertyType>> tmp_props{{k, PropertyType::ANY}};
                co_await meta_.addEdgeLabelProperties(*edge_label_name_, tmp_props);
                auto updated = co_await meta_.getEdgeLabelDefById(elid);
                if (updated) {
                    edge_label_defs_[elid] = std::move(*updated);
                    for (const auto& pd : edge_label_defs_[elid].properties) {
                        if (pd.name == k) {
                            pid = pd.id;
                            break;
                        }
                    }
                }
            }
            co_await store_.putEdgeProperty(edge_id, elid, pid, valueToPropertyValue(vs.value));
        }
        co_return;
    }
    // Vertex target
    VertexId vid = INVALID_VERTEX_ID;
    if (item.var_name == start_var_)
        vid = start_vid;
    else if (has_relationship_ && item.var_name == end_var_)
        vid = end_vid;
    if (vid != INVALID_VERTEX_ID && std::holds_alternative<MapValue>(normalised)) {
        const auto& mv = std::get<MapValue>(normalised);
        for (const auto& [k, vs] : mv.entries) {
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
                co_await store_.putVertexProperty(vid, anon_label_id_, pid, valueToPropertyValue(vs.value));
            }
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
            if (it != edge_label_name_to_id_.end()) {
                edge_label_id_ = it->second;
            } else {
                // Edge label doesn't exist yet — create it
                EdgeLabelId created_id = co_await meta_.createEdgeLabel(*edge_label_name_, {});
                if (created_id != INVALID_EDGE_LABEL_ID) {
                    edge_label_id_ = created_id;
                    auto def = co_await meta_.getEdgeLabelDefById(created_id);
                    if (def)
                        edge_label_defs_[created_id] = std::move(*def);
                    co_await store_.createEdgeLabel(created_id);
                }
            }
        }
        if (edge_label_id_.has_value())
            co_await ensureEdgeLabelTable(*edge_label_id_);
    }

    // Phase 2: register pending properties with __anon__
    co_await registerPendingProps(start_pending_props_, end_pending_props_, edge_pending_props_, on_create_items_,
                                  on_match_items_);

    // Phase 3: per-row processing — yield one chunk per row
    auto child_gen = child_->executeChunk();
    while (auto chunk = co_await child_gen.next()) {
        for (size_t row = 0; row < chunk->count; ++row) {
            bool start_created = false;
            bool end_created = false;
            bool edge_created = false;

            // Check for null property values in MERGE pattern — Cypher semantics require error
            {
                auto checkNullProp = [&](const auto& filters, const auto& pending) -> bool {
                    for (const auto& [_, expr] : filters) {
                        Value v = evaluateExpr(evaluator, expr, &*chunk, row);
                        if (std::holds_alternative<std::monostate>(v))
                            return true;
                    }
                    for (const auto& [_, expr] : pending) {
                        Value v = evaluateExpr(evaluator, expr, &*chunk, row);
                        if (std::holds_alternative<std::monostate>(v))
                            return true;
                    }
                    return false;
                };
                if (checkNullProp(start_prop_filters_, start_pending_props_) ||
                    (has_relationship_ && (checkNullProp(end_prop_filters_, end_pending_props_) ||
                                           checkNullProp(edge_prop_filters_, edge_pending_props_)))) {
                    throw std::runtime_error("SemanticError: MergeReadOwnWrites");
                }
            }

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

                spdlog::info("[MERGE] edge phase: start_vid={} end_vid={} edge_label_id_={}", start_vid, end_vid,
                             edge_label_id_.value_or(INVALID_EDGE_LABEL_ID));
                auto found_edge =
                    co_await findMatchingEdge(start_vid, end_vid, edge_prop_filters_, &*chunk, row, evaluator);
                if (found_edge) {
                    std::tie(edge_id, edge_label) = *found_edge;
                    spdlog::info("[MERGE] found existing edge: eid={} label={}", edge_id, edge_label);
                } else {
                    std::tie(edge_id, edge_label) = co_await createEdge(start_vid, end_vid, edge_prop_filters_,
                                                                        edge_pending_props_, &*chunk, row, evaluator);
                    edge_created = true;
                    spdlog::info("[MERGE] created edge: eid={} label={}", edge_id, edge_label);
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
                co_await executeSetItems(on_create_items_, merged, evaluator, start_vid, end_vid, edge_id);
            } else if (!any_created && !on_match_items_.empty()) {
                co_await executeSetItems(on_match_items_, merged, evaluator, start_vid, end_vid, edge_id);
            }

            // Collect labels added by SET_LABELS items
            auto collectAddedLabels = [&](const std::vector<SetPhysicalOp::BoundSetItem>& set_items,
                                          const std::string& var_name) -> std::vector<LabelId> {
                std::vector<LabelId> added;
                for (const auto& item : set_items) {
                    if (item.kind == cypher::SetItemKind::SET_LABELS && item.var_name == var_name &&
                        item.resolved_label_id) {
                        added.push_back(*item.resolved_label_id);
                    }
                }
                return added;
            };
            const auto& applied_items = any_created ? on_create_items_ : on_match_items_;
            auto start_added = collectAddedLabels(applied_items, start_var_);
            auto end_added = has_relationship_ ? collectAddedLabels(applied_items, end_var_) : std::vector<LabelId>();

            // Step 4: Fetch properties and build output chunk (one row)
            // Must fetch AFTER SET items so ON CREATE/MATCH modifications are visible.
            auto buildVertexValue = [&](VertexId vid, const std::vector<LabelId>& labels,
                                        const std::vector<LabelId>& extra_labels) -> folly::coro::Task<VertexValue> {
                VertexValue vv;
                vv.id = vid;
                vv.labels = LabelIdSet(labels.begin(), labels.end());
                for (auto lid : extra_labels)
                    vv.labels->insert(lid);
                auto all_labels = labels;
                for (auto lid : extra_labels)
                    all_labels.push_back(lid);
                for (auto lid : all_labels) {
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
                start_vv = co_await buildVertexValue(start_vid, start_labels_, start_added);
            VertexValue end_vv;
            if (has_relationship_) {
                if (end_pre_bound_ && end_pre_vv)
                    end_vv = *end_pre_vv;
                else if (!end_pre_bound_)
                    end_vv = co_await buildVertexValue(end_vid, end_labels_, end_added);
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
