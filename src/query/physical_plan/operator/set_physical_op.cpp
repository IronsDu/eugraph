#include "query/physical_plan/operator/set_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/cell_access.hpp"
#include "query/evaluator/expression_evaluator.hpp"
#include "query/physical_plan/operator/edge_index_maintenance.hpp"
#include "query/physical_plan/operator/mutation_mirror.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"
#include "query/physical_plan/operator/vertex_index_maintenance.hpp"
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

int findColumn(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

} // anonymous namespace

folly::coro::AsyncGenerator<DataChunk> SetPhysicalOp::executeChunk() {
    auto child_gen = cancellable(child_->executeChunkTraced());

    std::vector<const binder::BoundExpression*> prefetch_exprs;
    prefetch_exprs.reserve(items_.size());
    for (const auto& item : items_) {
        if (item.value)
            prefetch_exprs.push_back(&*item.value);
    }

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        // Pre-evaluate all value expressions for this chunk.
        std::vector<std::vector<Value>> value_results(items_.size());
        ExpressionEvaluator evaluator(eval_ctx_);
        co_await evaluator.prefetch(prefetch_exprs, *chunk);
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
                copyColumnCell(column, i, new_col, i);
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
                if (std::holds_alternative<EdgeValuePtr>(val)) {
                    if (item.kind == cypher::SetItemKind::SET_LABELS)
                        continue;

                    EdgeValue edge = (*std::get<EdgeValuePtr>(val));
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
                            std::vector<std::pair<std::string, PropertyType>> prop_defs_init;
                            prop_defs_init.emplace_back(item.prop_name, propertyValueToPropertyType(pv_init));
                            co_await meta_.addEdgeLabelProperties(eldef.name, prop_defs_init);
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

                        auto old_index_entries = co_await collectEdgeIndexEntries(store_, edge_label_defs_, eid, elid);

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
                        auto new_index_entries = co_await collectEdgeIndexEntries(store_, edge_label_defs_, eid, elid);
                        co_await deleteEdgeIndexEntries(store_, old_index_entries);
                        co_await insertEdgeIndexEntries(store_, new_index_entries);
                        mirrorEdgeToAllReferences(*chunk, static_cast<size_t>(col), row_idx,
                                                  mk<EdgeValue>(std::move(edge)));
                        continue;
                    }

                    if (item.kind == cypher::SetItemKind::SET_PROPERTIES) {
                        if (!item.value.has_value())
                            continue;
                        Value v = value_results[idx][row_idx];
                        if (!std::holds_alternative<MapValuePtr>(v))
                            continue;
                        const auto& mv = (*std::get<MapValuePtr>(v));
                        bool modified = false;
                        auto old_index_entries = co_await collectEdgeIndexEntries(store_, edge_label_defs_, eid, elid);

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
                                std::vector<std::pair<std::string, PropertyType>> prop_defs_reg;
                                prop_defs_reg.emplace_back(key, propertyValueToPropertyType(pv_reg));
                                co_await meta_.addEdgeLabelProperties(eldef.name, prop_defs_reg);
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

                        if (modified) {
                            auto new_index_entries =
                                co_await collectEdgeIndexEntries(store_, edge_label_defs_, eid, elid);
                            co_await deleteEdgeIndexEntries(store_, old_index_entries);
                            co_await insertEdgeIndexEntries(store_, new_index_entries);
                            mirrorEdgeToAllReferences(*chunk, static_cast<size_t>(col), row_idx,
                                                      mk<EdgeValue>(std::move(edge)));
                        }
                        continue;
                    }
                    continue;
                }

                if (!std::holds_alternative<VertexValuePtr>(val))
                    continue;

                const auto& vertex = (*std::get<VertexValuePtr>(val));
                VertexId vid = vertex.id;
                std::optional<std::vector<VertexIndexEntry>> old_index_entries;

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
                            const_cast<std::unordered_map<std::string, LabelId>&>(label_name_to_id_)[item.label] =
                                new_lid;
                            LabelDef def;
                            def.id = new_lid;
                            def.name = item.label;
                            label_defs_[new_lid] = std::move(def);
                            lit = label_name_to_id_.find(item.label);
                        }
                    }
                    if (lit == label_name_to_id_.end())
                        continue;
                    if (vertex.labels.has_value() && vertex.labels->count(lit->second))
                        continue;
                    co_await store_.addVertexLabel(vid, lit->second);
                    auto new_index_entries =
                        co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                    bool inserted = co_await insertVertexIndexEntriesChecked(store_, new_index_entries);
                    if (!inserted) {
                        co_await deleteVertexIndexEntries(store_, new_index_entries);
                        co_await store_.removeVertexLabel(vid, lit->second);
                        throw std::runtime_error("ConstraintVerificationFailed: unique index constraint violated");
                    }
                    VertexValue updated = vertex;
                    if (!updated.labels.has_value())
                        updated.labels = LabelIdSet{};
                    updated.labels->insert(lit->second);
                    mirrorVertexToAllReferences(*chunk, static_cast<size_t>(col), row_idx,
                                                mk<VertexValue>(std::move(updated)));
                } else if (item.kind == cypher::SetItemKind::SET_PROPERTY) {
                    if (item.prop_name.empty() || !item.value.has_value())
                        continue;
                    old_index_entries = co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);

                    Value v = value_results[idx][row_idx];

                    // Cypher null semantics: SET n.p = null ≡ REMOVE n.p
                    if (std::holds_alternative<std::monostate>(v)) {
                        // Remove every copy: all declared columns plus the weak
                        // space. Removal is the only write where touching both
                        // spaces is correct (it is lossless w.r.t. "p is gone").
                        struct RemovedProp {
                            LabelId lid;
                            uint16_t pid;
                            std::optional<PropertyValue> old;
                        };
                        std::vector<RemovedProp> removed;
                        auto remove_at = [&](LabelId lid, uint16_t pid) -> folly::coro::Task<void> {
                            auto old_props = co_await store_.getVertexProperties(vid, lid);
                            std::optional<PropertyValue> old;
                            if (old_props && old_props->size() > pid && (*old_props)[pid].has_value())
                                old = (*old_props)[pid].value();
                            co_await store_.deleteVertexProperty(vid, lid, pid);
                            removed.push_back(RemovedProp{lid, pid, std::move(old)});
                        };
                        if (item.strong_mode && item.resolved_label_id && item.resolved_prop_id) {
                            co_await remove_at(*item.resolved_label_id, *item.resolved_prop_id);
                        } else {
                            bool any_declared = false;
                            if (vertex.labels.has_value()) {
                                for (LabelId lid : *vertex.labels) {
                                    if (lid == anon_label_id_)
                                        continue;
                                    auto def_it = label_defs_.find(lid);
                                    if (def_it == label_defs_.end())
                                        continue;
                                    for (const auto& pd : def_it->second.properties) {
                                        if (pd.name != item.prop_name)
                                            continue;
                                        co_await remove_at(lid, pd.id);
                                        any_declared = true;
                                        break;
                                    }
                                }
                            }
                            if (!any_declared && anon_label_id_ != INVALID_LABEL_ID) {
                                auto anon_it = label_defs_.find(anon_label_id_);
                                if (anon_it != label_defs_.end()) {
                                    for (const auto& pd : anon_it->second.properties) {
                                        if (pd.name == item.prop_name) {
                                            co_await remove_at(anon_label_id_, pd.id);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (!removed.empty()) {
                            VertexValue updated = vertex;
                            for (const auto& r : removed) {
                                auto it = updated.properties.find(r.lid);
                                if (it != updated.properties.end() && it->second.size() > r.pid)
                                    it->second[r.pid].reset();
                            }
                            mirrorVertexToAllReferences(*chunk, static_cast<size_t>(col), row_idx,
                                                        mk<VertexValue>(std::move(updated)));
                        }
                        auto new_index_entries =
                            co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                        co_await deleteVertexIndexEntries(store_, *old_index_entries);
                        bool inserted = co_await insertVertexIndexEntriesChecked(store_, new_index_entries);
                        if (!inserted && !removed.empty() && old_index_entries) {
                            for (const auto& r : removed) {
                                if (r.old.has_value())
                                    co_await store_.putVertexProperty(vid, r.lid, r.pid, *r.old);
                                else
                                    co_await store_.deleteVertexProperty(vid, r.lid, r.pid);
                            }
                            auto restored =
                                co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                            co_await deleteVertexIndexEntries(store_, new_index_entries);
                            co_await insertVertexIndexEntries(store_, restored);
                            throw std::runtime_error("ConstraintVerificationFailed: unique index constraint violated");
                        }
                        continue;
                    }

                    PropertyValue pv = valueToPropertyValue(v);

                    // Determine which (label_id, prop_id) the value is written to,
                    // so we can mirror the mutation into the in-memory vertex state
                    // and make subsequent RETURN/ WITH clauses observe the new value.
                    std::optional<std::pair<LabelId, uint16_t>> written_at;
                    std::optional<PropertyValue> rollback_value;

                    if (item.strong_mode && item.resolved_label_id && item.resolved_prop_id) {
                        // Strong mode: the label scope is explicit, but the vertex
                        // must actually carry that label. Writing a property row for
                        // a missing label creates an orphan row: invisible to
                        // MATCH (n:L) and properties(n), readable only through the
                        // matching n::L.p accessor.
                        LabelIdSet current_labels =
                            vertex.labels.has_value() ? *vertex.labels : co_await store_.getVertexLabels(vid);
                        if (!current_labels.count(*item.resolved_label_id)) {
                            const auto def_it = label_defs_.find(*item.resolved_label_id);
                            const std::string label_name = def_it != label_defs_.end()
                                                               ? def_it->second.name
                                                               : std::to_string(*item.resolved_label_id);
                            throw std::runtime_error(
                                "Cannot write property '" + item.prop_name + "' on label '" + label_name +
                                "': the vertex does not have that label. Add it first with SET n:" + label_name +
                                ", or write n." + item.prop_name + " (weak mode).");
                        }
                        auto old_props = co_await store_.getVertexProperties(vid, *item.resolved_label_id);
                        if (old_props && old_props->size() > *item.resolved_prop_id &&
                            (*old_props)[*item.resolved_prop_id].has_value())
                            rollback_value = (*old_props)[*item.resolved_prop_id].value();
                        co_await store_.putVertexProperty(vid, *item.resolved_label_id, *item.resolved_prop_id, pv);
                        written_at = std::make_pair(*item.resolved_label_id, *item.resolved_prop_id);
                    } else {
                        // Convenience mode: declare-scope first, weak space as the
                        // fallback. __anon__ is never a peer candidate (it holds the
                        // weak keys of every vertex, so treating it as a peer would
                        // make every declared key look ambiguous).
                        std::vector<std::pair<LabelId, uint16_t>> matches;
                        if (vertex.labels.has_value()) {
                            for (LabelId lid : *vertex.labels) {
                                if (lid == anon_label_id_)
                                    continue;
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
                            auto old_props = co_await store_.getVertexProperties(vid, matches[0].first);
                            if (old_props && old_props->size() > matches[0].second &&
                                (*old_props)[matches[0].second].has_value())
                                rollback_value = (*old_props)[matches[0].second].value();
                            co_await store_.putVertexProperty(vid, matches[0].first, matches[0].second, pv);
                            written_at = matches[0];
                        } else if (matches.empty()) {
                            // No label declares this key: it belongs to the weak
                            // space (docs/features/property-ownership.md).
                            if (anon_label_id_ != INVALID_LABEL_ID) {
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
                            std::string label_names;
                            for (const auto& [lid, pid] : matches) {
                                auto it = label_defs_.find(lid);
                                if (it != label_defs_.end()) {
                                    if (!label_names.empty())
                                        label_names += ", ";
                                    label_names += it->second.name;
                                }
                            }
                            throw std::runtime_error("AmbiguousProperty: property '" + item.prop_name +
                                                     "' is declared by multiple labels of this vertex (" + label_names +
                                                     "). Use SET n::Label." + item.prop_name + " = ...");
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
                        mirrorVertexToAllReferences(*chunk, static_cast<size_t>(col), row_idx,
                                                    mk<VertexValue>(std::move(updated)));
                    }

                    auto new_index_entries =
                        co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                    co_await deleteVertexIndexEntries(store_, *old_index_entries);
                    bool inserted = co_await insertVertexIndexEntriesChecked(store_, new_index_entries);
                    if (!inserted && written_at && old_index_entries) {
                        if (rollback_value.has_value())
                            co_await store_.putVertexProperty(vid, written_at->first, written_at->second,
                                                              *rollback_value);
                        else
                            co_await store_.deleteVertexProperty(vid, written_at->first, written_at->second);
                        auto restored = co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                        co_await deleteVertexIndexEntries(store_, new_index_entries);
                        co_await insertVertexIndexEntries(store_, restored);
                        throw std::runtime_error("ConstraintVerificationFailed: unique index constraint violated");
                    }
                } else if (item.kind == cypher::SetItemKind::SET_PROPERTIES) {
                    if (!item.value.has_value())
                        continue;

                    Value v = value_results[idx][row_idx];
                    if (!std::holds_alternative<MapValuePtr>(v))
                        continue;
                    const auto& mv = (*std::get<MapValuePtr>(v));
                    old_index_entries = co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);

                    LabelIdSet old_labels = co_await store_.getVertexLabels(vid);
                    if (anon_label_id_ != INVALID_LABEL_ID && label_defs_.count(anon_label_id_))
                        old_labels.insert(anon_label_id_);
                    std::vector<std::tuple<LabelId, uint16_t, PropertyValue>> old_snapshot;
                    for (LabelId lid : old_labels) {
                        auto old_props = co_await store_.getVertexProperties(vid, lid);
                        if (!old_props)
                            continue;
                        for (size_t pid = 0; pid < old_props->size(); ++pid) {
                            if ((*old_props)[pid].has_value())
                                old_snapshot.emplace_back(lid, static_cast<uint16_t>(pid), (*old_props)[pid].value());
                        }
                    }

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
                                // Remove every copy of the key (declared columns,
                                // then the weak space when no column declares it).
                                bool any_declared = false;
                                if (vertex.labels.has_value()) {
                                    for (LabelId lid : *vertex.labels) {
                                        if (lid == anon_label_id_)
                                            continue;
                                        auto def_it = label_defs_.find(lid);
                                        if (def_it == label_defs_.end())
                                            continue;
                                        for (const auto& pd : def_it->second.properties) {
                                            if (pd.name != key)
                                                continue;
                                            co_await store_.deleteVertexProperty(vid, lid, pd.id);
                                            auto& props_vec = updated_vertex.properties[lid];
                                            if (props_vec.size() > pd.id)
                                                props_vec[pd.id].reset();
                                            vertex_modified = true;
                                            any_declared = true;
                                            break;
                                        }
                                    }
                                }
                                if (!any_declared && anon_label_id_ != INVALID_LABEL_ID) {
                                    auto anon_it = label_defs_.find(anon_label_id_);
                                    if (anon_it != label_defs_.end()) {
                                        for (const auto& pd : anon_it->second.properties) {
                                            if (pd.name != key)
                                                continue;
                                            co_await store_.deleteVertexProperty(vid, anon_label_id_, pd.id);
                                            auto& props_vec = updated_vertex.properties[anon_label_id_];
                                            if (props_vec.size() > pd.id)
                                                props_vec[pd.id].reset();
                                            vertex_modified = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            continue;
                        }
                        PropertyValue pv = valueToPropertyValue(entry_val);

                        std::vector<std::pair<LabelId, uint16_t>> matches;
                        if (vertex.labels.has_value()) {
                            for (LabelId lid : *vertex.labels) {
                                if (lid == anon_label_id_)
                                    continue;
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
                            // No label declares this key: weak space.
                            if (anon_label_id_ != INVALID_LABEL_ID) {
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
                            throw std::runtime_error("AmbiguousProperty: property '" + key +
                                                     "' is declared by multiple labels of this vertex (" + label_names +
                                                     "). Use SET n::Label." + key + " = ...");
                        }
                    }

                    // Write updated vertex back to chunk so RETURN sees the mutated state
                    if (vertex_modified)
                        mirrorVertexToAllReferences(*chunk, static_cast<size_t>(col), row_idx,
                                                    mk<VertexValue>(std::move(updated_vertex)));

                    auto new_index_entries =
                        co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                    co_await deleteVertexIndexEntries(store_, *old_index_entries);
                    bool inserted = co_await insertVertexIndexEntriesChecked(store_, new_index_entries);
                    if (!inserted) {
                        LabelIdSet current_labels = co_await store_.getVertexLabels(vid);
                        for (LabelId lid : current_labels)
                            if (!old_labels.count(lid))
                                co_await store_.removeVertexLabel(vid, lid);
                        for (LabelId lid : old_labels)
                            co_await store_.addVertexLabel(vid, lid);

                        for (LabelId lid : current_labels) {
                            auto props = co_await store_.getVertexProperties(vid, lid);
                            if (props) {
                                for (size_t pid = 0; pid < props->size(); ++pid) {
                                    if ((*props)[pid].has_value())
                                        co_await store_.deleteVertexProperty(vid, lid, static_cast<uint16_t>(pid));
                                }
                            }
                        }
                        for (const auto& [lid, pid, pv] : old_snapshot)
                            co_await store_.putVertexProperty(vid, lid, pid, pv);

                        auto restored = co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                        co_await deleteVertexIndexEntries(store_, new_index_entries);
                        co_await insertVertexIndexEntries(store_, restored);
                        throw std::runtime_error("ConstraintVerificationFailed: unique index constraint violated");
                    }
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
