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
    else if (std::holds_alternative<VertexValue>(v))
        vid = std::get<VertexValue>(v).id;
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
    } else if (std::holds_alternative<EdgeValue>(v)) {
        auto& ev = std::get<EdgeValue>(v);
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
        }
        s += ">";
    }
    s += "])";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> ProjectionExtractPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        const size_t row_count = chunk->numRows();
        const size_t n_specs = specs_.size();

        DataChunk output;
        output.columns.reserve(n_specs);
        for (const auto& spec : specs_) {
            binder::BoundTypeKind kind = spec.output_type.kind;
            if (spec.kind == ColumnSpec::Kind::Passthrough && spec.source_col < chunk->columns.size())
                kind = chunk->columns[spec.source_col].type;
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
            std::vector<size_t> row_map;
            row_map.reserve(row_count);
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
                } else if (src_edge_values) {
                    const auto& ev = src_col.buffer->edge_data[row];
                    eid = ev.id;
                    elid = ev.label_id;
                } else {
                    const auto& v = src_col.getValue(row);
                    if (std::holds_alternative<EdgeKey>(v)) {
                        const auto& ek = std::get<EdgeKey>(v);
                        eid = ek.id;
                        elid = ek.label_id;
                    } else if (std::holds_alternative<EdgeValue>(v)) {
                        const auto& ev = std::get<EdgeValue>(v);
                        eid = ev.id;
                        elid = ev.label_id;
                    }
                }
                if (eid == INVALID_EDGE_ID || elid != spec.edge_label_id)
                    continue;
                auto [it, inserted] = first_row_for_id.emplace(eid, ids.size());
                if (!inserted) {
                    row_map.push_back(it->second);
                } else {
                    ids.push_back(eid);
                    row_map.push_back(ids.size() - 1);
                }
            }
            edge_prop_cache[i].resize(row_count);
            if (ids.empty())
                continue;
            auto values = co_await store_.getEdgePropertyBatch(spec.edge_label_id, ids, spec.prop_id);
            for (size_t row = 0; row < row_count; ++row) {
                if (row < row_map.size() && row_map[row] < values.size())
                    edge_prop_cache[i][row] = std::move(values[row_map[row]]);
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

        // Prefetch vertex labels + full properties for ConstructVertex specs in
        // batch, instead of one labels fetch and one properties fetch per row.
        std::vector<std::vector<std::optional<VertexValue>>> vertex_ctor_cache(n_specs);
        for (size_t i = 0; i < n_specs; ++i) {
            const auto& spec = specs_[i];
            if (spec.kind != ColumnSpec::Kind::ConstructVertex)
                continue;
            vertex_ctor_cache[i].resize(row_count);

            std::vector<VertexId> ref_vids;
            std::vector<size_t> ref_rows;
            std::vector<size_t> anon_rows;
            for (size_t row = 0; row < row_count; ++row) {
                const auto& v = chunk->columns[spec.source_col].getValue(row);
                if (std::holds_alternative<VertexValue>(v)) {
                    VertexValue vv = std::get<VertexValue>(v);
                    if (vv.labels.has_value()) {
                        LabelIdSet labels = *vv.labels;
                        labels.erase(INVALID_LABEL_ID);
                        vv.labels = std::move(labels);
                    }
                    vertex_ctor_cache[i][row] = std::move(vv);
                    if (anon_label_id_ != INVALID_LABEL_ID &&
                        !vertex_ctor_cache[i][row]->properties.count(anon_label_id_))
                        anon_rows.push_back(row);
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
                std::unordered_map<LabelId, std::vector<size_t>> rows_by_label;
                for (size_t j = 0; j < ref_vids.size() && j < labels_batch.size(); ++j) {
                    VertexValue vv;
                    vv.id = ref_vids[j];
                    LabelIdSet labels = labels_batch[j];
                    labels.erase(INVALID_LABEL_ID);
                    vv.labels = labels;
                    vertex_ctor_cache[i][ref_rows[j]] = std::move(vv);
                    for (LabelId lid : labels)
                        rows_by_label[lid].push_back(j);
                }

                for (const auto& [lid, local_rows] : rows_by_label) {
                    std::vector<VertexId> ids;
                    ids.reserve(local_rows.size());
                    for (size_t j : local_rows)
                        ids.push_back(ref_vids[j]);
                    auto props = co_await store_.batchGetVertexProperties(ids, lid, {});
                    for (size_t j = 0; j < ids.size() && j < props.size(); ++j) {
                        if (props[j].has_value())
                            vertex_ctor_cache[i][ref_rows[local_rows[j]]]->properties[lid] = std::move(*props[j]);
                    }
                }

                if (anon_label_id_ != INVALID_LABEL_ID) {
                    auto anon_props = co_await store_.batchGetVertexProperties(ref_vids, anon_label_id_, {});
                    for (size_t j = 0; j < ref_vids.size() && j < anon_props.size(); ++j) {
                        if (anon_props[j].has_value())
                            vertex_ctor_cache[i][ref_rows[j]]->properties[anon_label_id_] = std::move(*anon_props[j]);
                    }
                }
            }

            if (!anon_rows.empty()) {
                std::vector<VertexId> ids;
                ids.reserve(anon_rows.size());
                for (size_t row : anon_rows)
                    ids.push_back(vertex_ctor_cache[i][row]->id);
                auto anon_props = co_await store_.batchGetVertexProperties(ids, anon_label_id_, {});
                for (size_t j = 0; j < ids.size() && j < anon_props.size(); ++j) {
                    if (anon_props[j].has_value())
                        vertex_ctor_cache[i][anon_rows[j]]->properties[anon_label_id_] = std::move(*anon_props[j]);
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
        };

        for (size_t row = 0; row < row_count; ++row) {
            RowCache rc;
            for (size_t i = 0; i < n_specs; ++i) {
                const auto& spec = specs_[i];
                switch (spec.kind) {
                case ColumnSpec::Kind::Passthrough: {
                    output.columns[i].setValue(row, chunk->columns[spec.source_col].getValue(row));
                    break;
                }
                case ColumnSpec::Kind::LoadVertexProp: {
                    VertexId vid = resolveVertexId(*chunk, spec.source_col, row, rc.vid_col, rc.vid);
                    if (vid != INVALID_VERTEX_ID) {
                        auto pv = co_await store_.getVertexProperty(vid, spec.label_id, spec.prop_id);
                        if (pv.has_value())
                            output.columns[i].setValue(row, propertyValueToValue(*pv));
                        else
                            output.columns[i].setNull(row);
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
                    VertexId vid = resolveVertexId(*chunk, spec.source_col, row, rc.vid_col, rc.vid);
                    if (vid == INVALID_VERTEX_ID)
                        break;
                    auto lit = rc.labels.find(spec.source_col);
                    if (lit == rc.labels.end()) {
                        auto labels = co_await store_.getVertexLabels(vid);
                        labels.erase(INVALID_LABEL_ID);
                        lit = rc.labels.emplace(spec.source_col, std::move(labels)).first;
                    }
                    ListValue lv;
                    for (auto lid : lit->second) {
                        auto nit = vertex_label_names_.find(lid);
                        if (nit != vertex_label_names_.end())
                            lv.elements.push_back(ValueStorage{Value(nit->second)});
                    }
                    output.columns[i].setValue(row, Value(std::move(lv)));
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
                    if (row < vertex_ctor_cache[i].size() && vertex_ctor_cache[i][row].has_value()) {
                        output.columns[i].setValue(row, Value(*vertex_ctor_cache[i][row]));
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
                        } else if (std::holds_alternative<EdgeValue>(v)) {
                            ev = std::get<EdgeValue>(v);
                        } else {
                            break;
                        }
                        if (ev.id == INVALID_EDGE_ID || ev.label_id == INVALID_EDGE_LABEL_ID)
                            break;
                        if (!ev.properties.has_value()) {
                            auto props = co_await store_.getEdgeProperties(ev.label_id, ev.id);
                            if (props.has_value())
                                ev.properties = std::move(*props);
                        }
                        rc.eid_col = spec.source_col;
                        rc.eid = ev.id;
                        rc.eid_label = ev.label_id;
                        eit = rc.edge_obj.emplace(spec.source_col, std::move(ev)).first;
                    }
                    output.columns[i].setValue(row, Value(eit->second));
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
