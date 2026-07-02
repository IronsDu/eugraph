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
        for (const auto& spec : specs_)
            output.columns.push_back(Column::flat(spec.output_type.kind, row_count));
        output.count = row_count;

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
                    }
                    break;
                }
                case ColumnSpec::Kind::LoadEdgeProp: {
                    EdgeId eid = resolveEdgeId(*chunk, spec.source_col, row, rc.eid_col, rc.eid, rc.eid_label);
                    if (eid != INVALID_EDGE_ID && rc.eid_label != INVALID_EDGE_LABEL_ID) {
                        auto pv = co_await store_.getEdgeProperty(rc.eid_label, eid, spec.prop_id);
                        if (pv.has_value())
                            output.columns[i].setValue(row, propertyValueToValue(*pv));
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
                    VertexId vid = resolveVertexId(*chunk, spec.source_col, row, rc.vid_col, rc.vid);
                    if (vid == INVALID_VERTEX_ID)
                        break;
                    auto vit = rc.vertex_obj.find(spec.source_col);
                    if (vit == rc.vertex_obj.end()) {
                        VertexValue vv;
                        vv.id = vid;
                        auto labels = co_await store_.getVertexLabels(vid);
                        labels.erase(INVALID_LABEL_ID);
                        vv.labels = labels;
                        for (auto lid : labels) {
                            auto props = co_await store_.getVertexProperties(vid, lid);
                            if (props.has_value())
                                vv.properties[lid] = std::move(*props);
                        }
                        rc.labels.emplace(spec.source_col, std::move(labels));
                        vit = rc.vertex_obj.emplace(spec.source_col, std::move(vv)).first;
                    }
                    output.columns[i].setValue(row, Value(vit->second));
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
