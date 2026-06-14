#include "query/physical_plan/operator/set_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

namespace {

void ensureLabelDefProp(std::unordered_map<LabelId, LabelDef>& label_defs, LabelId lid, uint16_t pid,
                        const std::string& prop_name, PropertyType prop_type) {
    auto it = label_defs.find(lid);
    if (it == label_defs.end())
        return;
    auto& props = it->second.properties;
    for (const auto& pd : props)
        if (pd.id == pid || pd.name == prop_name)
            return;
    PropertyDef pd;
    pd.id = pid;
    pd.name = prop_name;
    pd.type = prop_type;
    props.push_back(std::move(pd));
}

PropertyValue valueToPropertyValue(const Value& v) {
    if (std::holds_alternative<std::monostate>(v))
        return PropertyValue{};
    if (std::holds_alternative<bool>(v))
        return PropertyValue(std::get<bool>(v));
    if (std::holds_alternative<int64_t>(v))
        return PropertyValue(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))
        return PropertyValue(std::get<double>(v));
    if (std::holds_alternative<std::string>(v))
        return PropertyValue(std::get<std::string>(v));
    if (std::holds_alternative<DateTimeValue>(v))
        return PropertyValue(std::get<DateTimeValue>(v));
    if (std::holds_alternative<TimeValue>(v))
        return PropertyValue(std::get<TimeValue>(v));
    if (std::holds_alternative<DurationValue>(v))
        return PropertyValue(std::get<DurationValue>(v));
    if (std::holds_alternative<ListValue>(v)) {
        const auto& lv = std::get<ListValue>(v);
        if (lv.elements.empty())
            return PropertyValue{};
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
        } else if (std::holds_alternative<DateTimeValue>(first)) {
            std::vector<DateTimeValue> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<DateTimeValue>(e.value))
                    arr.push_back(std::get<DateTimeValue>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<TimeValue>(first)) {
            std::vector<TimeValue> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<TimeValue>(e.value))
                    arr.push_back(std::get<TimeValue>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<DurationValue>(first)) {
            std::vector<DurationValue> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<DurationValue>(e.value))
                    arr.push_back(std::get<DurationValue>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        }
    }
    return PropertyValue{};
}

int findColumn(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

} // anonymous namespace

folly::coro::AsyncGenerator<DataChunk> SetPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        // Pre-evaluate all value expressions for this chunk.
        std::vector<std::vector<Value>> value_results(items_.size());
        VectorizedEvaluator evaluator(eval_ctx_);
        for (size_t idx = 0; idx < items_.size(); ++idx) {
            if (items_[idx].value.has_value()) {
                auto col = Column::flat(binder::BoundTypeKind::ANY, n);
                evaluator.evaluate(*items_[idx].value, *chunk, col);
                value_results[idx].reserve(n);
                for (size_t r = 0; r < n; ++r) {
                    value_results[idx].push_back(col.getValue(r));
                }
            }
        }

        // Ensure vertex columns are writable (FLAT) before mutation.
        // DICTIONARY columns share data with other operators and are read-only;
        // CONSTANT columns have a single shared value.
        for (const auto& item : items_) {
            if (item.var_name.empty())
                continue;
            int col = findColumn(input_schema_, item.var_name);
            if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                continue;
            auto& column = chunk->columns[static_cast<size_t>(col)];
            if (column.type != binder::BoundTypeKind::VERTEX)
                continue;
            if (column.form == VectorForm::FLAT)
                continue;
            // Copy into a new FLAT column
            auto new_col = Column::flat(binder::BoundTypeKind::VERTEX, n);
            for (size_t i = 0; i < n; ++i)
                new_col.setValue(i, column.getValue(i));
            column = std::move(new_col);
        }

        for (size_t row_idx = 0; row_idx < n; ++row_idx) {
            for (size_t idx = 0; idx < items_.size(); ++idx) {
                const auto& item = items_[idx];
                if (item.var_name.empty())
                    continue;

                int col = findColumn(input_schema_, item.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;

                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);
                if (!std::holds_alternative<VertexValue>(val))
                    continue;

                const auto& vertex = std::get<VertexValue>(val);
                VertexId vid = vertex.id;

                if (item.kind == cypher::SetItemKind::SET_LABELS) {
                    auto lit = label_name_to_id_.find(item.label);
                    if (lit == label_name_to_id_.end())
                        continue;
                    co_await store_.addVertexLabel(vid, lit->second);
                } else if (item.kind == cypher::SetItemKind::SET_PROPERTY) {
                    if (item.prop_name.empty() || !item.value.has_value())
                        continue;

                    Value v = value_results[idx][row_idx];
                    PropertyValue pv = valueToPropertyValue(v);

                    // Determine which (label_id, prop_id) the value is written to,
                    // so we can mirror the mutation into the in-memory vertex state
                    // and make subsequent RETURN/ WITH clauses observe the new value.
                    std::optional<std::pair<LabelId, uint16_t>> written_at;

                    if (item.strong_mode && item.resolved_label_id && item.resolved_prop_id) {
                        // Strong mode: use resolved IDs directly
                        co_await store_.putVertexProperty(vid, *item.resolved_label_id, *item.resolved_prop_id, pv);
                        written_at = std::make_pair(*item.resolved_label_id, *item.resolved_prop_id);
                    } else {
                        // Convenience mode: runtime inference based on actual labels
                        std::vector<std::pair<LabelId, uint16_t>> matches;
                        if (vertex.labels.has_value()) {
                            for (LabelId lid : *vertex.labels) {
                                auto def_it = label_defs_.find(lid);
                                if (def_it == label_defs_.end())
                                    continue;
                                for (const auto& pd : def_it->second.properties) {
                                    if (pd.name == item.prop_name) {
                                        matches.emplace_back(lid, pd.id);
                                        break;
                                    }
                                }
                            }
                        }

                        if (matches.size() == 1) {
                            co_await store_.putVertexProperty(vid, matches[0].first, matches[0].second, pv);
                            written_at = matches[0];
                        } else if (matches.empty()) {
                            // No match in actual labels: write to __anon__ via lightweight allocation
                            if (anon_label_id_ != INVALID_LABEL_ID) {
                                uint16_t anon_pid = co_await meta_.getOrCreateAnonPropId(
                                    item.prop_name, propertyValueToPropertyType(pv));
                                co_await store_.putVertexProperty(vid, anon_label_id_, anon_pid, pv);
                                ensureLabelDefProp(label_defs_, anon_label_id_, anon_pid, item.prop_name,
                                                   propertyValueToPropertyType(pv));
                                written_at = std::make_pair(anon_label_id_, anon_pid);
                            }
                        } else {
                            // Multiple matches: runtime error
                            std::string label_names;
                            for (const auto& [lid, pid] : matches) {
                                auto it = label_defs_.find(lid);
                                if (it != label_defs_.end()) {
                                    if (!label_names.empty())
                                        label_names += ", ";
                                    label_names += it->second.name;
                                }
                            }
                            spdlog::error("Ambiguous property '{}' found in labels: {}. Use ::Label to specify.",
                                          item.prop_name, label_names);
                        }
                    }

                    // Mirror the write into the in-memory vertex so that any
                    // subsequent RETURN/WITH in the same query observes the
                    // updated property without re-reading from the store.
                    if (written_at) {
                        VertexValue updated = vertex;
                        auto& props_vec = updated.properties[written_at->first];
                        if (props_vec.size() <= written_at->second)
                            props_vec.resize(written_at->second + 1);
                        props_vec[written_at->second] = pv;
                        chunk->setValue(static_cast<size_t>(col), row_idx, Value(std::move(updated)));
                    }
                } else if (item.kind == cypher::SetItemKind::SET_PROPERTIES) {
                    if (!item.value.has_value())
                        continue;

                    Value v = value_results[idx][row_idx];
                    if (!std::holds_alternative<MapValue>(v))
                        continue;
                    const auto& mv = std::get<MapValue>(v);

                    // Keep a mutable copy of the vertex to update in-memory state
                    // after store mutations, so the returned row reflects the changes.
                    VertexValue updated_vertex = vertex;
                    bool vertex_modified = false;

                    // For '=' (replace): delete all existing vertex properties first.
                    // Query the store directly to find all properties regardless of
                    // what projection pushdown loaded into the in-memory vertex.
                    if (!item.is_add_assign) {
                        LabelIdSet all_lids = co_await store_.getVertexLabels(vid);
                        if (anon_label_id_ != INVALID_LABEL_ID)
                            all_lids.insert(anon_label_id_);
                        for (LabelId lid : all_lids) {
                            auto props = co_await store_.getVertexProperties(vid, lid);
                            if (props) {
                                for (size_t pid = 0; pid < props->size(); ++pid) {
                                    if ((*props)[pid].has_value())
                                        co_await store_.deleteVertexProperty(vid, lid, static_cast<uint16_t>(pid));
                                }
                            }
                        }
                        updated_vertex.properties.clear();
                        vertex_modified = true;
                    }

                    // Write each map entry via convenience mode resolution
                    for (const auto& [key, vs] : mv.entries) {
                        Value entry_val = vs.value;
                        // null map values remove the property
                        if (std::holds_alternative<std::monostate>(entry_val))
                            continue;
                        PropertyValue pv = valueToPropertyValue(entry_val);

                        std::vector<std::pair<LabelId, uint16_t>> matches;
                        if (vertex.labels.has_value()) {
                            for (LabelId lid : *vertex.labels) {
                                auto def_it = label_defs_.find(lid);
                                if (def_it == label_defs_.end())
                                    continue;
                                for (const auto& pd : def_it->second.properties) {
                                    if (pd.name == key) {
                                        matches.emplace_back(lid, pd.id);
                                        break;
                                    }
                                }
                            }
                        }

                        if (matches.size() == 1) {
                            co_await store_.putVertexProperty(vid, matches[0].first, matches[0].second, pv);
                            auto& props_vec = updated_vertex.properties[matches[0].first];
                            if (props_vec.size() <= matches[0].second)
                                props_vec.resize(matches[0].second + 1);
                            props_vec[matches[0].second] = pv;
                            vertex_modified = true;
                        } else if (matches.empty()) {
                            if (anon_label_id_ != INVALID_LABEL_ID) {
                                uint16_t anon_pid =
                                    co_await meta_.getOrCreateAnonPropId(key, propertyValueToPropertyType(pv));
                                co_await store_.putVertexProperty(vid, anon_label_id_, anon_pid, pv);
                                ensureLabelDefProp(label_defs_, anon_label_id_, anon_pid, key,
                                                   propertyValueToPropertyType(pv));
                                auto& props_vec = updated_vertex.properties[anon_label_id_];
                                if (props_vec.size() <= anon_pid)
                                    props_vec.resize(anon_pid + 1);
                                props_vec[anon_pid] = pv;
                                vertex_modified = true;
                            }
                        } else {
                            std::string label_names;
                            for (const auto& [lid, pid] : matches) {
                                auto it = label_defs_.find(lid);
                                if (it != label_defs_.end()) {
                                    if (!label_names.empty())
                                        label_names += ", ";
                                    label_names += it->second.name;
                                }
                            }
                            spdlog::error("Ambiguous property '{}' found in labels: {}. Use ::Label to specify.", key,
                                          label_names);
                        }
                    }

                    // Write updated vertex back to chunk so RETURN sees the mutated state
                    if (vertex_modified)
                        chunk->setValue(static_cast<size_t>(col), row_idx, Value(std::move(updated_vertex)));
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
