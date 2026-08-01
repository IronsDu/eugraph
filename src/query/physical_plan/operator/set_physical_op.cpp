#include "query/physical_plan/operator/set_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/physical_plan/operator/mutation_mirror.hpp"
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
        } else if (std::holds_alternative<MapValue>(first)) {
            throw std::runtime_error("TypeError: InvalidPropertyType: list of maps is not supported as a property value");
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

        // Ensure vertex/edge columns are writable (FLAT) before mutation.
        // DICTIONARY columns share data with other operators and are read-only;
        // CONSTANT columns have a single shared value.
        for (const auto& item : items_) {
            if (item.var_name.empty())
                continue;
            int col = item.object_col >= 0 ? item.object_col : findColumn(input_schema_, item.var_name);
            if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                continue;
            auto& column = chunk->columns[static_cast<size_t>(col)];
            if (column.type != binder::BoundTypeKind::VERTEX && column.type != binder::BoundTypeKind::EDGE)
                continue;
            if (column.form == VectorForm::FLAT)
                continue;
            // Copy into a new FLAT column
            auto new_col = Column::flat(column.type, n);
            for (size_t i = 0; i < n; ++i)
                new_col.setValue(i, column.getValue(i));
            column = std::move(new_col);
        }

        for (size_t row_idx = 0; row_idx < n; ++row_idx) {
            for (size_t idx = 0; idx < items_.size(); ++idx) {
                const auto& item = items_[idx];
                if (item.var_name.empty())
                    continue;

                int col = item.object_col >= 0 ? item.object_col : findColumn(input_schema_, item.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;

                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);

                // Edge handling: SET on edges. SET_LABELS does not apply (edges
                // have a single label), so it is silently skipped.
                if (std::holds_alternative<EdgeValue>(val)) {
                    if (item.kind == cypher::SetItemKind::SET_LABELS)
                        continue;

                    EdgeValue edge = std::get<EdgeValue>(val);
                    EdgeId eid = edge.id;
                    EdgeLabelId elid = edge.label_id;

                    auto def_it = edge_label_defs_.find(elid);
                    if (def_it == edge_label_defs_.end()) {
                        auto loaded = co_await meta_.getEdgeLabelDefById(elid);
                        if (!loaded)
                            continue;
                        edge_label_defs_[elid] = std::move(*loaded);
                        def_it = edge_label_defs_.find(elid);
                    }
                    const EdgeLabelDef& eldef = def_it->second;

                    if (item.kind == cypher::SetItemKind::SET_PROPERTY) {
                        if (item.prop_name.empty() || !item.value.has_value())
                            continue;
                        Value v = value_results[idx][row_idx];

                        // Resolve (or dynamically register) prop_id by name
                        uint16_t pid = UINT16_MAX;
                        for (const auto& pd : eldef.properties) {
                            if (pd.name == item.prop_name) {
                                pid = pd.id;
                                break;
                            }
                        }
                        if (pid == UINT16_MAX) {
                            PropertyValue pv_init = valueToPropertyValue(v);
                            co_await meta_.addEdgeLabelProperties(
                                eldef.name, {{item.prop_name, propertyValueToPropertyType(pv_init)}});
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
                            if (pid == UINT16_MAX)
                                continue;
                        }

                        if (std::holds_alternative<std::monostate>(v)) {
                            // Cypher null semantics: SET r.p = null ≡ REMOVE r.p
                            co_await store_.deleteEdgeProperty(eid, elid, pid);
                            if (edge.properties.has_value() && edge.properties->size() > pid)
                                (*edge.properties)[pid].reset();
                        } else {
                            PropertyValue pv = valueToPropertyValue(v);
                            co_await store_.putEdgeProperty(eid, elid, pid, pv);
                            if (!edge.properties.has_value())
                                edge.properties = Properties{};
                            if (edge.properties->size() <= pid)
                                edge.properties->resize(pid + 1);
                            (*edge.properties)[pid] = pv;
                        }
                        mirrorEdgeToAllReferences(*chunk, static_cast<size_t>(col), row_idx, std::move(edge));
                        continue;
                    }

                    if (item.kind == cypher::SetItemKind::SET_PROPERTIES) {
                        if (!item.value.has_value())
                            continue;
                        Value v = value_results[idx][row_idx];
                        if (!std::holds_alternative<MapValue>(v))
                            continue;
                        const auto& mv = std::get<MapValue>(v);
                        bool modified = false;

                        // '=' mode: delete all existing edge properties first
                        if (!item.is_add_assign) {
                            for (const auto& pd : eldef.properties) {
                                co_await store_.deleteEdgeProperty(eid, elid, pd.id);
                            }
                            if (edge.properties.has_value())
                                edge.properties->clear();
                            modified = true;
                        }

                        for (const auto& [key, vs_entry] : mv.entries) {
                            Value entry_val = vs_entry.value;
                            // null: += → REMOVE; = → skip (already cleared)
                            if (std::holds_alternative<std::monostate>(entry_val)) {
                                if (item.is_add_assign) {
                                    uint16_t null_pid = UINT16_MAX;
                                    for (const auto& pd : eldef.properties) {
                                        if (pd.name == key) {
                                            null_pid = pd.id;
                                            break;
                                        }
                                    }
                                    if (null_pid != UINT16_MAX) {
                                        co_await store_.deleteEdgeProperty(eid, elid, null_pid);
                                        if (edge.properties.has_value() && edge.properties->size() > null_pid)
                                            (*edge.properties)[null_pid].reset();
                                        modified = true;
                                    }
                                }
                                continue;
                            }

                            // Resolve (or dynamically register) prop_id
                            uint16_t entry_pid = UINT16_MAX;
                            for (const auto& pd : eldef.properties) {
                                if (pd.name == key) {
                                    entry_pid = pd.id;
                                    break;
                                }
                            }
                            if (entry_pid == UINT16_MAX) {
                                PropertyValue pv_reg = valueToPropertyValue(entry_val);
                                co_await meta_.addEdgeLabelProperties(
                                    eldef.name, {{key, propertyValueToPropertyType(pv_reg)}});
                                auto updated = co_await meta_.getEdgeLabelDefById(elid);
                                if (updated) {
                                    edge_label_defs_[elid] = std::move(*updated);
                                    for (const auto& pd : edge_label_defs_[elid].properties) {
                                        if (pd.name == key) {
                                            entry_pid = pd.id;
                                            break;
                                        }
                                    }
                                }
                                if (entry_pid == UINT16_MAX)
                                    continue;
                            }

                            PropertyValue pv = valueToPropertyValue(entry_val);
                            co_await store_.putEdgeProperty(eid, elid, entry_pid, pv);
                            if (!edge.properties.has_value())
                                edge.properties = Properties{};
                            if (edge.properties->size() <= entry_pid)
                                edge.properties->resize(entry_pid + 1);
                            (*edge.properties)[entry_pid] = pv;
                            modified = true;
                        }

                        if (modified)
                            mirrorEdgeToAllReferences(*chunk, static_cast<size_t>(col), row_idx, std::move(edge));
                        continue;
                    }
                    continue;
                }

                if (!std::holds_alternative<VertexValue>(val))
                    continue;

                const auto& vertex = std::get<VertexValue>(val);
                VertexId vid = vertex.id;

                if (item.kind == cypher::SetItemKind::SET_LABELS) {
                    auto lit = label_name_to_id_.find(item.label);
                    spdlog::info("[SetPhysicalOp] SET_LABELS label='{}' found_in_map={}", item.label,
                                 lit != label_name_to_id_.end());
                    if (lit == label_name_to_id_.end()) {
                        // Auto-create the label dynamically so SET n:NewLabel works
                        // without prior definition (mirrors CREATE (:NewLabel) Phase 0).
                        auto new_lid = co_await meta_.createLabel(item.label, {});
                        if (new_lid != INVALID_LABEL_ID) {
                            co_await store_.createLabel(new_lid);
                            const_cast<std::unordered_map<std::string, LabelId>&>(label_name_to_id_)[item.label] = new_lid;
                            LabelDef def;
                            def.id = new_lid;
                            def.name = item.label;
                            label_defs_[new_lid] = std::move(def);
                            lit = label_name_to_id_.find(item.label);
                        }
                    }
                    if (lit == label_name_to_id_.end())
                        continue;
                    co_await store_.addVertexLabel(vid, lit->second);
                    VertexValue updated = vertex;
                    if (!updated.labels.has_value())
                        updated.labels = LabelIdSet{};
                    updated.labels->insert(lit->second);
                    mirrorVertexToAllReferences(*chunk, static_cast<size_t>(col), row_idx, std::move(updated));
                } else if (item.kind == cypher::SetItemKind::SET_PROPERTY) {
                    if (item.prop_name.empty() || !item.value.has_value())
                        continue;

                    Value v = value_results[idx][row_idx];

                    // Cypher null semantics: SET n.p = null ≡ REMOVE n.p
                    if (std::holds_alternative<std::monostate>(v)) {
                        std::optional<std::pair<LabelId, uint16_t>> removed_at;
                        if (item.strong_mode && item.resolved_label_id && item.resolved_prop_id) {
                            co_await store_.deleteVertexProperty(vid, *item.resolved_label_id, *item.resolved_prop_id);
                            removed_at = std::make_pair(*item.resolved_label_id, *item.resolved_prop_id);
                        } else {
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
                                co_await store_.deleteVertexProperty(vid, matches[0].first, matches[0].second);
                                removed_at = matches[0];
                            }
                            // 0 matches: no-op (REMOVE on non-existent is no-op)
                            // >1 matches: ambiguous, no-op (consistent with non-null path)
                        }
                        if (removed_at) {
                            VertexValue updated = vertex;
                            auto it = updated.properties.find(removed_at->first);
                            if (it != updated.properties.end() && it->second.size() > removed_at->second)
                                it->second[removed_at->second].reset();
                            mirrorVertexToAllReferences(*chunk, static_cast<size_t>(col), row_idx, std::move(updated));
                        }
                        continue;
                    }

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
                            // No match in actual labels: register on the first user label
                            // (skip __anon__) so the new property shares the vertex's
                            // primary label rather than being shunted to __anon__.
                            // Falls back to __anon__ only when the vertex has no user labels.
                            LabelId target_lid = INVALID_LABEL_ID;
                            if (vertex.labels.has_value()) {
                                for (LabelId lid : *vertex.labels) {
                                    if (lid == anon_label_id_)
                                        continue;
                                    target_lid = lid;
                                    break;
                                }
                            }
                            if (target_lid != INVALID_LABEL_ID) {
                                auto def_it = label_defs_.find(target_lid);
                                if (def_it != label_defs_.end()) {
                                    co_await meta_.addVertexLabelProperties(
                                        def_it->second.name,
                                        {{item.prop_name, propertyValueToPropertyType(pv)}});
                                    auto updated = co_await meta_.getLabelDefById(target_lid);
                                    uint16_t new_pid = UINT16_MAX;
                                    if (updated) {
                                        label_defs_[target_lid] = std::move(*updated);
                                        for (const auto& pd : label_defs_[target_lid].properties) {
                                            if (pd.name == item.prop_name) {
                                                new_pid = pd.id;
                                                break;
                                            }
                                        }
                                    }
                                    if (new_pid != UINT16_MAX) {
                                        co_await store_.putVertexProperty(vid, target_lid, new_pid, pv);
                                        written_at = std::make_pair(target_lid, new_pid);
                                    }
                                }
                            } else if (anon_label_id_ != INVALID_LABEL_ID) {
                                // Vertex has no user labels: write to __anon__.
                                uint16_t anon_pid = co_await meta_.getOrCreateAnonPropId(
                                    item.prop_name, propertyValueToPropertyType(pv));
                                if (!vertex.labels.has_value() ||
                                    vertex.labels->find(anon_label_id_) == vertex.labels->end())
                                    co_await store_.addVertexLabel(vid, anon_label_id_);
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
                        // If we wrote to a label the vertex didn't have, mirror the
                        // label addition in-memory so labels(...) / ConstructVertex
                        // consumers downstream see it without re-reading the store.
                        if (written_at->first == anon_label_id_ &&
                            (!updated.labels.has_value() ||
                             updated.labels->find(anon_label_id_) == updated.labels->end())) {
                            if (!updated.labels.has_value())
                                updated.labels = LabelIdSet{};
                            updated.labels->insert(anon_label_id_);
                        }
                        auto& props_vec = updated.properties[written_at->first];
                        if (props_vec.size() <= written_at->second)
                            props_vec.resize(written_at->second + 1);
                        props_vec[written_at->second] = pv;
                        mirrorVertexToAllReferences(*chunk, static_cast<size_t>(col), row_idx, std::move(updated));
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
                        // null 处理（openCypher 规范）：
                        //   += 模式: 显式 REMOVE 该属性
                        //   =  模式: 跳过写入（前面 delete-all 已清空所有旧属性，
                        //           跳过 = 该 key 不存在，与规范一致）
                        if (std::holds_alternative<std::monostate>(entry_val)) {
                            if (item.is_add_assign) {
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
                                // 也检查 __anon__ 标签下是否定义了该属性
                                if (anon_label_id_ != INVALID_LABEL_ID) {
                                    auto anon_it = label_defs_.find(anon_label_id_);
                                    if (anon_it != label_defs_.end()) {
                                        for (const auto& pd : anon_it->second.properties) {
                                            if (pd.name == key) {
                                                matches.emplace_back(anon_label_id_, pd.id);
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (matches.size() == 1) {
                                    co_await store_.deleteVertexProperty(vid, matches[0].first, matches[0].second);
                                    auto& props_vec = updated_vertex.properties[matches[0].first];
                                    if (props_vec.size() > matches[0].second)
                                        props_vec[matches[0].second].reset();
                                    vertex_modified = true;
                                }
                                // 0 matches: no-op; >1 matches: ambiguous, no-op
                            }
                            continue;
                        }
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
                            // Register on the first user label (skip __anon__) so the new
                            // property shares the vertex's primary label. Falls back to
                            // __anon__ only when the vertex has no user labels.
                            LabelId target_lid = INVALID_LABEL_ID;
                            if (vertex.labels.has_value()) {
                                for (LabelId lid : *vertex.labels) {
                                    if (lid == anon_label_id_)
                                        continue;
                                    target_lid = lid;
                                    break;
                                }
                            }
                            if (target_lid != INVALID_LABEL_ID) {
                                auto def_it = label_defs_.find(target_lid);
                                if (def_it != label_defs_.end()) {
                                    co_await meta_.addVertexLabelProperties(
                                        def_it->second.name, {{key, propertyValueToPropertyType(pv)}});
                                    auto updated = co_await meta_.getLabelDefById(target_lid);
                                    uint16_t new_pid = UINT16_MAX;
                                    if (updated) {
                                        label_defs_[target_lid] = std::move(*updated);
                                        for (const auto& pd : label_defs_[target_lid].properties) {
                                            if (pd.name == key) {
                                                new_pid = pd.id;
                                                break;
                                            }
                                        }
                                    }
                                    if (new_pid != UINT16_MAX) {
                                        co_await store_.putVertexProperty(vid, target_lid, new_pid, pv);
                                        auto& props_vec = updated_vertex.properties[target_lid];
                                        if (props_vec.size() <= new_pid)
                                            props_vec.resize(new_pid + 1);
                                        props_vec[new_pid] = pv;
                                        vertex_modified = true;
                                    }
                                }
                            } else if (anon_label_id_ != INVALID_LABEL_ID) {
                                uint16_t anon_pid =
                                    co_await meta_.getOrCreateAnonPropId(key, propertyValueToPropertyType(pv));
                                if (!vertex.labels.has_value() ||
                                    vertex.labels->find(anon_label_id_) == vertex.labels->end())
                                    co_await store_.addVertexLabel(vid, anon_label_id_);
                                co_await store_.putVertexProperty(vid, anon_label_id_, anon_pid, pv);
                                ensureLabelDefProp(label_defs_, anon_label_id_, anon_pid, key,
                                                   propertyValueToPropertyType(pv));
                                if (!updated_vertex.labels.has_value())
                                    updated_vertex.labels = LabelIdSet{};
                                updated_vertex.labels->insert(anon_label_id_);
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
                        mirrorVertexToAllReferences(*chunk, static_cast<size_t>(col), row_idx,
                                                    std::move(updated_vertex));
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
