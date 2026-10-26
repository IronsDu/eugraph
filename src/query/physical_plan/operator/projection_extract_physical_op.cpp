#include "query/physical_plan/operator/projection_extract_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"

namespace eugraph {
namespace compute {

namespace {

/// Resolve a vertex id from a column. Returns INVALID_VERTEX_ID for null/
/// non-vertex values. Caches the last (col, vid) so multiple specs reading
/// the same source column within one row do not re-parse the Value variant.
inline VertexId resolveVertexId(const DataChunk& chunk, size_t col, size_t row, size_t& cached_col,
                                VertexId& cached_vid) {
    if (col == cached_col)
        return cached_vid;
    VertexId vid = INVALID_VERTEX_ID;
    auto v = chunk.columns[col].getValue(row);
    if (std::holds_alternative<VertexRef>(v))
        vid = std::get<VertexRef>(v).id;
    else if (std::holds_alternative<VertexValuePtr>(v))
        vid = (*std::get<VertexValuePtr>(v)).id;
    cached_col = col;
    cached_vid = vid;
    return vid;
}

/// Resolve edge id and label id from a column. Same caching strategy as
/// resolveVertexId. Returns INVALID_EDGE_ID for null/non-edge values.
inline EdgeId resolveEdgeId(const DataChunk& chunk, size_t col, size_t row, size_t& cached_col, EdgeId& cached_eid,
                            EdgeLabelId& cached_label) {
    if (col == cached_col)
        return cached_eid;
    EdgeId eid = INVALID_EDGE_ID;
    EdgeLabelId elid = INVALID_EDGE_LABEL_ID;
    auto v = chunk.columns[col].getValue(row);
    if (std::holds_alternative<EdgeKey>(v)) {
        auto& ek = std::get<EdgeKey>(v);
        eid = ek.id;
        elid = ek.label_id;
    } else if (std::holds_alternative<EdgeValuePtr>(v)) {
        auto& ev = (*std::get<EdgeValuePtr>(v));
        eid = ev.id;
        elid = ev.label_id;
    }
    cached_col = col;
    cached_eid = eid;
    cached_label = elid;
    return eid;
}

} // namespace

std::string ProjectionExtractPhysicalOp::toString() const {
    std::string s = "ProjectionExtract(specs=[";
    for (size_t i = 0; i < specs_.size(); ++i) {
        if (i > 0)
            s += ", ";
        const auto& sp = specs_[i];
        s += sp.output_name;
        s += "<";
        switch (sp.kind) {
        case ColumnSpec::Kind::Passthrough:
            s += "pass";
            break;
        case ColumnSpec::Kind::LoadVertexProp:
            s += "vprop ";
            s += std::to_string(sp.label_id);
            s += ".";
            s += std::to_string(sp.prop_id);
            break;
        case ColumnSpec::Kind::LoadEdgeProp:
            s += "eprop ";
            s += std::to_string(sp.edge_label_id);
            s += ".";
            s += std::to_string(sp.prop_id);
            break;
        case ColumnSpec::Kind::LoadVertexLabels:
            s += "vlabels";
            break;
        case ColumnSpec::Kind::LoadEdgeType:
            s += "etype";
            break;
        case ColumnSpec::Kind::ConstructVertex:
            s += "ctor-vertex";
            break;
        case ColumnSpec::Kind::ConstructEdge:
            s += "ctor-edge";
            break;
        case ColumnSpec::Kind::LoadVertexPropCoalesce:
            s += "vprop-coalesce[";
            for (size_t c = 0; c < sp.coalesce_candidates.size(); ++c) {
                if (c > 0)
                    s += ",";
                s += std::to_string(sp.coalesce_candidates[c].first) + "." +
                     std::to_string(sp.coalesce_candidates[c].second);
            }
            s += "]";
            break;
        }
        s += ">";
    }
    s += "])";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> ProjectionExtractPhysicalOp::executeChunk() {
    auto child_gen = cancellable(child_->executeChunk());
    // Reused across chunks/specs: ConstructVertex's vid->VertexValue maps.
    std::unordered_map<VertexId, VertexValuePtr> ctor_by_vid;
    std::unordered_map<VertexId, VertexValuePtr> ctor_anon_ptrs;

    while (auto chunk = co_await child_gen.next()) {
        const size_t row_count = chunk->numRows();
        const size_t n_specs = specs_.size();

        DataChunk output;
        output.columns.reserve(n_specs);
        for (const auto& spec : specs_) {
            binder::BoundTypeKind kind = spec.output_type.kind;
            if (spec.kind == ColumnSpec::Kind::Passthrough && spec.source_col < chunk->columns.size()) {
                const Column& src = chunk->columns[spec.source_col];
                kind = src.type;
                // Keep a broadcast source broadcast. This operator maps input rows
                // to output rows one for one, so a constant column stays constant;
                // materialising it into a FLAT column instead costs row_count deep
                // copies of the same value. When that value is a collected list --
                // LDBC complex-5/6/9 all do `WITH collect(...) AS xs UNWIND xs AS x`
                // and then never read xs again -- those copies are what turned a
                // 671 MB query into one that exceeded 6 GB.
                if (src.isConstant()) {
                    output.columns.push_back(Column::constant(src.getValue(0)));
                    continue;
                }
            }
            output.columns.push_back(Column::flat(kind, row_count));
        }
        output.count = row_count;

        // Prefetch edge properties per LoadEdgeProp spec in one IO dispatch,
        // instead of awaiting a single-edge fetch for every row.
        std::vector<std::vector<std::optional<PropertyValue>>> edge_prop_cache(n_specs);
        for (size_t i = 0; i < n_specs; ++i) {
            const auto& spec = specs_[i];
            if (spec.kind != ColumnSpec::Kind::LoadEdgeProp)
                continue;
            std::vector<EdgeId> ids;
            ids.reserve(row_count);
            std::vector<size_t> value_for_row(row_count, SIZE_MAX);
            std::unordered_map<EdgeId, size_t> first_row_for_id;
            const Column& src_col = chunk->columns[spec.source_col];
            const bool src_edge_keys =
                src_col.form == VectorForm::FLAT && src_col.buffer && !src_col.buffer->edge_key_data.empty();
            const bool src_edge_values =
                src_col.form == VectorForm::FLAT && src_col.buffer && !src_col.buffer->edge_data.empty();
            for (size_t row = 0; row < row_count; ++row) {
                EdgeId eid = INVALID_EDGE_ID;
                EdgeLabelId elid = INVALID_EDGE_LABEL_ID;
                if (src_edge_keys) {
                    const auto& ek = src_col.buffer->edge_key_data[row];
                    eid = ek.id;
                    elid = ek.label_id;
                } else if (src_edge_values && row < src_col.buffer->edge_data.size() &&
                           src_col.buffer->edge_data[row]) {
                    // The column holds handles now; reserve() leaves a slot empty, so
                    // check it before binding the payload.
                    const auto& ev = *src_col.buffer->edge_data[row];
                    eid = ev.id;
                    elid = ev.label_id;
                } else {
                    const auto& v = src_col.getValue(row);
                    if (std::holds_alternative<EdgeKey>(v)) {
                        const auto& ek = std::get<EdgeKey>(v);
                        eid = ek.id;
                        elid = ek.label_id;
                    } else if (std::holds_alternative<EdgeValuePtr>(v)) {
                        const auto& ev = (*std::get<EdgeValuePtr>(v));
                        eid = ev.id;
                        elid = ev.label_id;
                    }
                }
                if (eid == INVALID_EDGE_ID || elid != spec.edge_label_id)
                    continue;
                auto [it, inserted] = first_row_for_id.emplace(eid, ids.size());
                if (!inserted) {
                    value_for_row[row] = it->second;
                } else {
                    ids.push_back(eid);
                    value_for_row[row] = ids.size() - 1;
                }
            }
            edge_prop_cache[i].resize(row_count);
            if (ids.empty())
                continue;
            auto values = co_await store_.getEdgePropertyBatch(spec.edge_label_id, ids, spec.prop_id);
            for (size_t row = 0; row < row_count; ++row) {
                const size_t vidx = value_for_row[row];
                // Duplicate rows share one value slot; copy (not move) so
                // every row keeps the property value.
                if (vidx != SIZE_MAX && vidx < values.size())
                    edge_prop_cache[i][row] = values[vidx];
            }

            // If every present value is an integer, publish the output column
            // as typed INT64 so downstream predicates avoid Value dispatch.
            bool any_value = false;
            bool all_int64 = true;
            for (const auto& pv : edge_prop_cache[i]) {
                if (!pv.has_value())
                    continue;
                any_value = true;
                if (!std::holds_alternative<int64_t>(*pv)) {
                    all_int64 = false;
                    break;
                }
            }
            if (any_value && all_int64) {
                output.columns[i].type = binder::BoundTypeKind::INT64;
                output.columns[i].buffer->int64_data.resize(row_count);
            }
        }

        // Batch-prefetch single-property / label reads as well: LoadVertexProp
        // used to issue one co_await per row, so an Expand of 52k messages with
        // friend.firstName/lastName meant 100k+ async point reads. Collect the
        // unique ids per spec, issue one batch per spec, then fan the results
        // back out to rows.
        std::vector<std::vector<std::optional<PropertyValue>>> vertex_prop_cache(n_specs);
        std::vector<std::vector<std::optional<LabelIdSet>>> vertex_labels_cache(n_specs);
        for (size_t i = 0; i < n_specs; ++i) {
            const auto& spec = specs_[i];
            if (spec.kind != ColumnSpec::Kind::LoadVertexProp && spec.kind != ColumnSpec::Kind::LoadVertexLabels)
                continue;

            std::vector<VertexId> ids;
            std::vector<size_t> value_for_row(row_count, SIZE_MAX);
            std::unordered_map<VertexId, size_t> index_of;
            index_of.reserve(row_count);
            for (size_t row = 0; row < row_count; ++row) {
                // resolveVertexId caches by column only; a fresh pair per row is
                // required here because this loop runs before the per-row cache.
                size_t vid_cache_col = SIZE_MAX;
                VertexId vid_cache = INVALID_VERTEX_ID;
                VertexId vid = resolveVertexId(*chunk, spec.source_col, row, vid_cache_col, vid_cache);
                if (vid == INVALID_VERTEX_ID)
                    continue;
                auto [it, inserted] = index_of.try_emplace(vid, ids.size());
                if (inserted)
                    ids.push_back(vid);
                value_for_row[row] = it->second;
            }
            if (ids.empty())
                continue;

            if (spec.kind == ColumnSpec::Kind::LoadVertexProp) {
                auto values =
                    co_await store_.batchGetVertexProperties(ids, spec.label_id, std::vector<uint16_t>{spec.prop_id});
                vertex_prop_cache[i].resize(row_count);
                for (size_t row = 0; row < row_count; ++row) {
                    const size_t idx = value_for_row[row];
                    if (idx == SIZE_MAX || idx >= values.size() || !values[idx].has_value())
                        continue;
                    const auto& props = *values[idx];
                    if (props.size() > spec.prop_id && props[spec.prop_id].has_value())
                        vertex_prop_cache[i][row] = props[spec.prop_id];
                }
            } else {
                auto labels = co_await store_.getVertexLabelsBatch(ids);
                vertex_labels_cache[i].resize(row_count);
                for (size_t row = 0; row < row_count; ++row) {
                    const size_t idx = value_for_row[row];
                    if (idx == SIZE_MAX || idx >= labels.size())
                        continue;
                    LabelIdSet ids_set = labels[idx];
                    ids_set.erase(INVALID_LABEL_ID);
                    vertex_labels_cache[i][row] = std::move(ids_set);
                }
            }
        }

        // Prefetch coalesced multi-candidate vertex properties. Each row may
        // have values from several present labels; the runtime semantics are
        // scalar if exactly one label has the property, or a list if multiple
        // labels on the same vertex define it.
        std::vector<std::vector<std::vector<PropertyValue>>> vertex_coalesce_cache(n_specs);
        for (size_t i = 0; i < n_specs; ++i) {
            const auto& spec = specs_[i];
            if (spec.kind != ColumnSpec::Kind::LoadVertexPropCoalesce)
                continue;
            vertex_coalesce_cache[i].resize(row_count);

            std::vector<VertexId> ref_vids;
            std::vector<size_t> ref_rows;
            for (size_t row = 0; row < row_count; ++row) {
                // resolveVertexId caches only by source column, so the cache
                // must be reset for each row to avoid reusing the previous
                // row's VertexId for every later row.
                size_t vid_cache_col = SIZE_MAX;
                VertexId vid_cache = INVALID_VERTEX_ID;
                VertexId vid = resolveVertexId(*chunk, spec.source_col, row, vid_cache_col, vid_cache);
                if (vid == INVALID_VERTEX_ID)
                    continue;
                ref_vids.push_back(vid);
                ref_rows.push_back(row);
            }
            if (ref_vids.empty())
                continue;

            auto coalesced = co_await store_.batchGetVertexCoalesceProperties(ref_vids, spec.coalesce_candidates);
            for (size_t j = 0; j < ref_vids.size() && j < coalesced.size(); ++j) {
                for (auto& value : coalesced[j])
                    vertex_coalesce_cache[i][ref_rows[j]].push_back(std::move(value));
            }
        }

        // Prefetch vertex labels + full properties for ConstructVertex specs in
        // batch, instead of one labels fetch and one properties fetch per row.
        // The cache stores **shared handles**: the same bound vertex repeated across
        // rows (friend/countryX/countryY after an Expand) is materialized once and
        // shared, instead of deep-copying its labels+properties map per row.
        std::vector<std::vector<VertexValuePtr>> vertex_ctor_cache(n_specs);
        for (size_t i = 0; i < n_specs; ++i) {
            const auto& spec = specs_[i];
            if (spec.kind != ColumnSpec::Kind::ConstructVertex)
                continue;
            vertex_ctor_cache[i].resize(row_count);

            std::vector<VertexId> ref_vids;
            std::vector<size_t> ref_rows;
            std::vector<size_t> anon_rows; // value-source rows that still need anon props
            for (size_t row = 0; row < row_count; ++row) {
                const auto& v = chunk->columns[spec.source_col].getValue(row);
                if (std::holds_alternative<VertexValuePtr>(v)) {
                    VertexValuePtr src = std::get<VertexValuePtr>(v);
                    const bool strip_invalid =
                        src && src->labels.has_value() && src->labels->count(INVALID_LABEL_ID) > 0;
                    const bool need_anon = anon_label_id_ != INVALID_LABEL_ID && src && src->labels.has_value() &&
                                           src->labels->count(anon_label_id_) > 0 &&
                                           src->properties.count(anon_label_id_) == 0;
                    if (src && !strip_invalid && !need_anon) {
                        // 只读路径：直接共享同一个 VertexValue ✓
                        vertex_ctor_cache[i][row] = std::move(src);
                    } else if (src) {
                        // 需要清理标签/补 anon 属性时才写时复制，避免污染其它行。
                        auto copy = std::make_shared<VertexValue>(*src);
                        if (copy->labels.has_value()) {
                            LabelIdSet labels = *copy->labels;
                            labels.erase(INVALID_LABEL_ID);
                            copy->labels = std::move(labels);
                        }
                        vertex_ctor_cache[i][row] = std::move(copy);
                        if (need_anon)
                            anon_rows.push_back(row);
                    }
                } else {
                    VertexId vid = INVALID_VERTEX_ID;
                    if (std::holds_alternative<VertexRef>(v))
                        vid = std::get<VertexRef>(v).id;
                    else if (std::holds_alternative<int64_t>(v))
                        vid = static_cast<VertexId>(std::get<int64_t>(v));
                    if (vid == INVALID_VERTEX_ID)
                        continue;
                    ref_vids.push_back(vid);
                    ref_rows.push_back(row);
                }
            }

            if (!ref_vids.empty()) {
                auto labels_batch = co_await store_.getVertexLabelsBatch(ref_vids);
                auto& by_vid = ctor_by_vid;
                by_vid.clear();
                by_vid.reserve(ref_vids.size());
                for (size_t j = 0; j < ref_vids.size() && j < labels_batch.size(); ++j) {
                    const VertexId vid = ref_vids[j];
                    auto [it, inserted] = by_vid.try_emplace(vid);
                    if (inserted) {
                        auto vv = std::make_shared<VertexValue>();
                        vv->id = vid;
                        LabelIdSet labels = labels_batch[j];
                        labels.erase(INVALID_LABEL_ID);
                        vv->labels = std::move(labels);
                        it->second = std::move(vv);
                    }
                    vertex_ctor_cache[i][ref_rows[j]] = it->second;
                }

                std::unordered_map<LabelId, std::vector<VertexId>> ids_by_label;
                for (const auto& [vid, vv] : by_vid) {
                    if (!vv->labels.has_value())
                        continue;
                    for (LabelId lid : *vv->labels)
                        ids_by_label[lid].push_back(vid);
                }
                for (const auto& [lid, ids] : ids_by_label) {
                    auto props = co_await store_.batchGetVertexProperties(ids, lid, {});
                    for (size_t j = 0; j < ids.size() && j < props.size(); ++j) {
                        if (props[j].has_value())
                            by_vid[ids[j]]->properties[lid] = std::move(*props[j]);
                    }
                }

                if (anon_label_id_ != INVALID_LABEL_ID) {
                    // 只对真的带 anon 标签的 vid 查 anon 属性（避免每行一次空查询）。
                    std::vector<VertexId> anon_ids;
                    for (const auto& [vid, vv] : by_vid) {
                        if (vv->labels.has_value() && vv->labels->count(anon_label_id_) &&
                            !vv->properties.count(anon_label_id_))
                            anon_ids.push_back(vid);
                    }
                    if (!anon_ids.empty()) {
                        auto anon_props = co_await store_.batchGetVertexProperties(anon_ids, anon_label_id_, {});
                        for (size_t j = 0; j < anon_ids.size() && j < anon_props.size(); ++j) {
                            if (anon_props[j].has_value())
                                by_vid[anon_ids[j]]->properties[anon_label_id_] = std::move(*anon_props[j]);
                        }
                    }
                }
            }

            if (!anon_rows.empty()) {
                auto& anon_ptrs = ctor_anon_ptrs;
                anon_ptrs.clear();
                std::vector<VertexId> anon_ids;
                for (size_t row : anon_rows) {
                    const VertexValuePtr& vv = vertex_ctor_cache[i][row];
                    if (!vv)
                        continue;
                    if (anon_ptrs.try_emplace(vv->id, vv).second)
                        anon_ids.push_back(vv->id);
                }
                if (!anon_ids.empty()) {
                    auto anon_props = co_await store_.batchGetVertexProperties(anon_ids, anon_label_id_, {});
                    for (size_t j = 0; j < anon_ids.size() && j < anon_props.size(); ++j) {
                        if (anon_props[j].has_value())
                            anon_ptrs[anon_ids[j]]->properties[anon_label_id_] = std::move(*anon_props[j]);
                    }
                }
            }
        }

        // Per-row caches so multiple specs reading the same source column share
        // the resolved id and (for Construct*) the constructed entity.
        // Per-row entity caches avoid re-issuing labels+properties I/O when
        // the same variable is consumed by multiple output columns.
        struct RowCache {
            size_t vid_col = SIZE_MAX;
            VertexId vid = INVALID_VERTEX_ID;
            size_t eid_col = SIZE_MAX;
            EdgeId eid = INVALID_EDGE_ID;
            EdgeLabelId eid_label = INVALID_EDGE_LABEL_ID;
            // source_col → constructed VertexValue (ConstructVertex)
            std::unordered_map<size_t, VertexValue> vertex_obj;
            // source_col → constructed EdgeValue (ConstructEdge)
            std::unordered_map<size_t, EdgeValue> edge_obj;
            // source_col → resolved labels (LoadVertexLabels / ConstructVertex)
            std::unordered_map<size_t, LabelIdSet> labels;

            /// Reset for the next row. The maps keep their bucket arrays, which is
            /// the point: constructing a fresh RowCache per row made every first
            /// insert allocate a bucket array, and the per-row maps are exactly
            /// where the profiler put the remaining cost (the emplace into
            /// edge_obj). Values are owned by the maps, so clearing releases them
            /// and leaves nothing dangling.
            void clear() {
                vid_col = SIZE_MAX;
                vid = INVALID_VERTEX_ID;
                eid_col = SIZE_MAX;
                eid = INVALID_EDGE_ID;
                eid_label = INVALID_EDGE_LABEL_ID;
                vertex_obj.clear();
                edge_obj.clear();
                labels.clear();
            }
        };

        // Reused across rows so the maps' bucket arrays are allocated once per
        // chunk rather than once per row.
        RowCache rc;
        for (size_t row = 0; row < row_count; ++row) {
            rc.clear();
            for (size_t i = 0; i < n_specs; ++i) {
                const auto& spec = specs_[i];
                switch (spec.kind) {
                case ColumnSpec::Kind::Passthrough: {
                    // A broadcast source was installed as a CONSTANT column above; its
                    // single value already serves every row, and writing to it would
                    // overwrite the broadcast for all of them.
                    if (output.columns[i].isConstant())
                        break;
                    // Pure pass-through: copy the typed payload directly instead of
                    // getValue (which builds a Value, deep-copying the payload) followed
                    // by setValue (which copies it again out of that Value).
                    if (!output.columns[i].copyValueFrom(chunk->columns[spec.source_col], row, row))
                        output.columns[i].setValue(row, chunk->columns[spec.source_col].getValue(row));
                    break;
                }
                case ColumnSpec::Kind::LoadVertexProp: {
                    if (row < vertex_prop_cache[i].size() && vertex_prop_cache[i][row].has_value())
                        output.columns[i].setValue(row, propertyValueToValue(*vertex_prop_cache[i][row]));
                    else
                        output.columns[i].setNull(row);
                    break;
                }
                case ColumnSpec::Kind::LoadVertexPropCoalesce: {
                    if (row < vertex_coalesce_cache[i].size()) {
                        const auto& values = vertex_coalesce_cache[i][row];
                        // A multi-label CREATE writes a shared property to every
                        // label that declares it. UNWIND/coalesce must still
                        // produce one Cypher property value, not a duplicate
                        // list; collapse identical per-label values (genuinely
                        // conflicting values stay a list, matching multi-label
                        // property semantics).
                        if (values.empty()) {
                            output.columns[i].setNull(row);
                        } else if (values.size() == 1) {
                            // Fast path: the common single-label case must not pay
                            // for a per-row dedup vector (complex-3 reads 52k
                            // creationDate values through this path).
                            output.columns[i].setValue(row, propertyValueToValue(values[0]));
                        } else {
                            std::vector<PropertyValue> unique_values;
                            unique_values.reserve(values.size());
                            for (const auto& pv : values) {
                                bool duplicate = false;
                                for (const auto& existing : unique_values) {
                                    if (existing == pv) {
                                        duplicate = true;
                                        break;
                                    }
                                }
                                if (!duplicate)
                                    unique_values.push_back(pv);
                            }
                            if (unique_values.size() == 1) {
                                output.columns[i].setValue(row, propertyValueToValue(unique_values[0]));
                            } else {
                                ListValue lv;
                                lv.elements.reserve(unique_values.size());
                                for (const auto& pv : unique_values)
                                    lv.elements.push_back(ValueStorage{propertyValueToValue(pv)});
                                output.columns[i].setValue(row, Value(mk<ListValue>(std::move(lv))));
                            }
                        }
                    } else {
                        output.columns[i].setNull(row);
                    }
                    break;
                }
                case ColumnSpec::Kind::LoadEdgeProp: {
                    if (row < edge_prop_cache[i].size()) {
                        const auto& pv = edge_prop_cache[i][row];
                        if (!pv.has_value()) {
                            output.columns[i].setNull(row);
                        } else if (output.columns[i].type == binder::BoundTypeKind::INT64 &&
                                   std::holds_alternative<int64_t>(*pv)) {
                            output.columns[i].buffer->int64_data[row] = std::get<int64_t>(*pv);
                        } else {
                            output.columns[i].setValue(row, propertyValueToValue(*pv));
                        }
                    } else {
                        output.columns[i].setNull(row);
                    }
                    break;
                }
                case ColumnSpec::Kind::LoadVertexLabels: {
                    if (row >= vertex_labels_cache[i].size() || !vertex_labels_cache[i][row].has_value())
                        break;
                    ListValue lv;
                    for (auto lid : *vertex_labels_cache[i][row]) {
                        auto nit = vertex_label_names_.find(lid);
                        if (nit != vertex_label_names_.end())
                            lv.elements.push_back(ValueStorage{Value(nit->second)});
                    }
                    output.columns[i].setValue(row, Value(mk<ListValue>(std::move(lv))));
                    break;
                }
                case ColumnSpec::Kind::LoadEdgeType: {
                    EdgeId eid = resolveEdgeId(*chunk, spec.source_col, row, rc.eid_col, rc.eid, rc.eid_label);
                    if (eid == INVALID_EDGE_ID || rc.eid_label == INVALID_EDGE_LABEL_ID)
                        break;
                    auto nit = edge_label_names_.find(rc.eid_label);
                    if (nit != edge_label_names_.end())
                        output.columns[i].setValue(row, Value(nit->second));
                    break;
                }
                case ColumnSpec::Kind::ConstructVertex: {
                    if (row < vertex_ctor_cache[i].size() && vertex_ctor_cache[i][row]) {
                        // 同一 (spec, vid) 在批内共享同一个 VertexValue ✓；这里只发布
                        // handle，不再按行 deep-copy（payload 是 labels+properties map）。
                        VertexValuePtr vv = vertex_ctor_cache[i][row];
                        if (!output.columns[i].setVertexValue(row, vv))
                            output.columns[i].setValue(row, Value(std::move(vv)));
                    } else {
                        output.columns[i].setNull(row);
                    }
                    break;
                }
                case ColumnSpec::Kind::ConstructEdge: {
                    auto eit = rc.edge_obj.find(spec.source_col);
                    if (eit == rc.edge_obj.end()) {
                        auto v = chunk->columns[spec.source_col].getValue(row);
                        EdgeValue ev;
                        if (std::holds_alternative<EdgeKey>(v)) {
                            const auto& ek = std::get<EdgeKey>(v);
                            ev.id = ek.id;
                            ev.src_id = ek.src_id;
                            ev.dst_id = ek.dst_id;
                            ev.label_id = ek.label_id;
                            ev.seq = ek.seq;
                        } else if (std::holds_alternative<EdgeValuePtr>(v)) {
                            ev = (*std::get<EdgeValuePtr>(v));
                        } else {
                            break;
                        }
                        if (ev.id == INVALID_EDGE_ID || ev.label_id == INVALID_EDGE_LABEL_ID)
                            break;
                        if (!ev.properties.has_value()) {
                            // 无属性边（如 HAS_CREATOR / IS_LOCATED_IN）不需要查 eprop 表：
                            // 大结果集上每行一次空查会放大成数万次 WT search。
                            auto pc = edge_prop_counts_.find(ev.label_id);
                            const bool may_have_props = pc == edge_prop_counts_.end() || pc->second > 0;
                            if (may_have_props) {
                                auto props = co_await store_.getEdgeProperties(ev.label_id, ev.id);
                                if (props.has_value())
                                    ev.properties = std::move(*props);
                            }
                        }
                        rc.eid_col = spec.source_col;
                        rc.eid = ev.id;
                        rc.eid_label = ev.label_id;
                        eit = rc.edge_obj.emplace(spec.source_col, std::move(ev)).first;
                    }
                    // eit->second may be reused by a later spec naming the same
                    // source column, so copy rather than move -- but assign the typed
                    // payload directly instead of wrapping it in a Value, which would
                    // deep-copy it once more.
                    auto ev = mk<EdgeValue>(eit->second);
                    if (!output.columns[i].setEdgeValue(row, ev))
                        output.columns[i].setValue(row, Value(std::move(ev)));
                    break;
                }
                }
            }
        }

        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
