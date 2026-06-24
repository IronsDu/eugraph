#include "query/physical_plan/operator/vertex_property_extract_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"

namespace eugraph {
namespace compute {

std::string VertexPropertyExtractPhysicalOp::toString() const {
    std::string s = "VertexPropertyExtract(var=" + variable_ + ", ";
    if (!label_requests_.empty())
        s += "labels, ";
    if (!vertex_requests_.empty())
        s += "vertex, ";
    s += "props=[";
    bool first = true;
    for (auto& req : prop_requests_) {
        if (!first)
            s += ",";
        first = false;
        s += std::to_string(req.label_id) + "." + std::to_string(req.prop_id);
    }
    s += "])";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> VertexPropertyExtractPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t input_cols = chunk->numColumns();
        size_t row_count = chunk->numRows();
        size_t nlabels = label_requests_.size();
        size_t nprops = prop_requests_.size();
        size_t nverts = vertex_requests_.size();
        size_t nnew = nlabels + nprops + nverts;

        std::vector<Column> new_cols(nnew);
        for (size_t i = 0; i < nlabels; ++i)
            new_cols[i] = Column::flat(binder::BoundTypeKind::LIST, row_count);
        for (size_t p = 0; p < nprops; ++p) {
            auto kind = output_types_[input_cols + nlabels + p].kind;
            new_cols[nlabels + p] = Column::flat(kind, row_count);
        }
        for (size_t v = 0; v < nverts; ++v)
            new_cols[nlabels + nprops + v] = Column::flat(binder::BoundTypeKind::VERTEX, row_count);

        VertexId cached_vid = INVALID_VERTEX_ID;
        size_t cached_col = SIZE_MAX;
        for (size_t row = 0; row < row_count; ++row) {
            // Labels.
            for (size_t li = 0; li < nlabels; ++li) {
                auto& lr = label_requests_[li];
                VertexId vid = INVALID_VERTEX_ID;
                if (lr.source_col == cached_col)
                    vid = cached_vid;
                else {
                    auto v = chunk->columns[lr.source_col].getValue(row);
                    if (std::holds_alternative<VertexRef>(v))
                        vid = std::get<VertexRef>(v).id;
                    else if (std::holds_alternative<VertexValue>(v))
                        vid = std::get<VertexValue>(v).id;
                    cached_vid = vid;
                    cached_col = lr.source_col;
                }
                if (vid == INVALID_VERTEX_ID)
                    continue;
                auto labels = co_await store_.getVertexLabels(vid);
                labels.erase(INVALID_LABEL_ID);
                ListValue lv;
                for (auto lid : labels) {
                    auto it = label_id_to_name_.find(lid);
                    if (it != label_id_to_name_.end())
                        lv.elements.push_back(ValueStorage{Value(it->second)});
                }
                new_cols[li].setValue(row, Value(std::move(lv)));
            }

            // Properties.
            for (size_t p = 0; p < nprops; ++p) {
                auto& pr = prop_requests_[p];
                VertexId vid = INVALID_VERTEX_ID;
                if (pr.source_col == cached_col)
                    vid = cached_vid;
                else {
                    auto v = chunk->columns[pr.source_col].getValue(row);
                    if (std::holds_alternative<VertexRef>(v))
                        vid = std::get<VertexRef>(v).id;
                    else if (std::holds_alternative<VertexValue>(v))
                        vid = std::get<VertexValue>(v).id;
                    cached_vid = vid;
                    cached_col = pr.source_col;
                }
                if (vid == INVALID_VERTEX_ID)
                    continue;
                auto pv = co_await store_.getVertexProperty(vid, pr.label_id, pr.prop_id);
                if (pv.has_value())
                    new_cols[nlabels + p].setValue(row, propertyValueToValue(*pv));
            }

            // Full vertices.
            for (size_t v = 0; v < nverts; ++v) {
                auto& vr = vertex_requests_[v];
                VertexId vid = INVALID_VERTEX_ID;
                if (vr.source_col == cached_col)
                    vid = cached_vid;
                else {
                    auto val = chunk->columns[vr.source_col].getValue(row);
                    if (std::holds_alternative<VertexRef>(val))
                        vid = std::get<VertexRef>(val).id;
                    else if (std::holds_alternative<VertexValue>(val))
                        vid = std::get<VertexValue>(val).id;
                    cached_vid = vid;
                    cached_col = vr.source_col;
                }
                if (vid == INVALID_VERTEX_ID)
                    continue;
                VertexValue vv;
                vv.id = vid;
                auto labels = co_await store_.getVertexLabels(vid);
                labels.erase(INVALID_LABEL_ID);
                vv.labels = std::move(labels);
                for (auto lid : vv.labels.value_or(LabelIdSet{})) {
                    auto p = co_await store_.getVertexProperties(vid, lid);
                    if (p)
                        vv.properties[lid] = std::move(*p);
                }
                new_cols[nlabels + nprops + v].setValue(row, Value(std::move(vv)));
            }
        }

        DataChunk output;
        output.columns = std::move(chunk->columns);
        for (auto& nc : new_cols)
            output.columns.push_back(std::move(nc));
        output.count = row_count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
