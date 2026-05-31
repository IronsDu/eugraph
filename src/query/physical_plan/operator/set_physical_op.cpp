#include "query/physical_plan/operator/set_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

namespace {

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

                    if (item.strong_mode && item.resolved_label_id && item.resolved_prop_id) {
                        // Strong mode: use resolved IDs directly
                        co_await store_.putVertexProperty(vid, *item.resolved_label_id, *item.resolved_prop_id, pv);
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
                        } else if (matches.empty()) {
                            // No match in actual labels: write to __anon__ via lightweight allocation
                            if (anon_label_id_ != INVALID_LABEL_ID) {
                                uint16_t anon_pid = co_await meta_.getOrCreateAnonPropId(
                                    item.prop_name, propertyValueToPropertyType(pv));
                                co_await store_.putVertexProperty(vid, anon_label_id_, anon_pid, pv);
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
                } else if (item.kind == cypher::SetItemKind::SET_PROPERTIES) {
                    if (!item.value.has_value())
                        continue;

                    Value v = value_results[idx][row_idx];
                    if (!std::holds_alternative<MapValue>(v))
                        continue;
                    const auto& mv = std::get<MapValue>(v);

                    // For '=' (replace): delete all existing vertex properties first
                    if (!item.is_add_assign) {
                        if (vertex.labels.has_value()) {
                            for (LabelId lid : *vertex.labels) {
                                auto def_it = label_defs_.find(lid);
                                if (def_it == label_defs_.end())
                                    continue;
                                for (const auto& pd : def_it->second.properties) {
                                    co_await store_.deleteVertexProperty(vid, lid, pd.id);
                                }
                            }
                        }
                        // Also delete anon properties
                        if (anon_label_id_ != INVALID_LABEL_ID) {
                            auto anon_it = label_defs_.find(anon_label_id_);
                            if (anon_it != label_defs_.end()) {
                                for (const auto& pd : anon_it->second.properties) {
                                    co_await store_.deleteVertexProperty(vid, anon_label_id_, pd.id);
                                }
                            }
                        }
                    }

                    // Write each map entry via convenience mode resolution
                    for (const auto& [key, vs] : mv.entries) {
                        Value entry_val = vs.value;
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
                        } else if (matches.empty()) {
                            if (anon_label_id_ != INVALID_LABEL_ID) {
                                uint16_t anon_pid = co_await meta_.getOrCreateAnonPropId(key, PropertyType::ANY);
                                co_await store_.putVertexProperty(vid, anon_label_id_, anon_pid, pv);
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
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
