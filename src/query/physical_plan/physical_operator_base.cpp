#include "query/physical_plan/physical_operator_base.hpp"

#include <algorithm>

namespace eugraph {
namespace compute {

namespace {

/// Column kind implied by a runtime Value. `nullptr` means "no opinion" (NULL or a
/// kind this helper does not classify), which callers treat as "keep looking".
const binder::BoundTypeKind* kindOf(const Value& v) {
    static const binder::BoundTypeKind kBool = binder::BoundTypeKind::BOOL;
    static const binder::BoundTypeKind kInt = binder::BoundTypeKind::INT64;
    static const binder::BoundTypeKind kDouble = binder::BoundTypeKind::DOUBLE;
    static const binder::BoundTypeKind kString = binder::BoundTypeKind::STRING;
    static const binder::BoundTypeKind kVertexRef = binder::BoundTypeKind::VERTEX_REF;
    static const binder::BoundTypeKind kEdgeKey = binder::BoundTypeKind::EDGE_KEY;
    static const binder::BoundTypeKind kPathTopology = binder::BoundTypeKind::PATH_TOPOLOGY;
    static const binder::BoundTypeKind kVertex = binder::BoundTypeKind::VERTEX;
    static const binder::BoundTypeKind kEdge = binder::BoundTypeKind::EDGE;
    static const binder::BoundTypeKind kPath = binder::BoundTypeKind::PATH;
    static const binder::BoundTypeKind kList = binder::BoundTypeKind::LIST;
    static const binder::BoundTypeKind kMap = binder::BoundTypeKind::MAP;
    static const binder::BoundTypeKind kDateTime = binder::BoundTypeKind::DATETIME;
    static const binder::BoundTypeKind kTime = binder::BoundTypeKind::TIME;
    static const binder::BoundTypeKind kDuration = binder::BoundTypeKind::DURATION;

    switch (v.index()) {
    case 1:
        return &kBool;
    case 2:
        return &kInt;
    case 3:
        return &kDouble;
    case 4:
        return &kString;
    case 5:
        return &kVertexRef;
    case 6:
        return &kEdgeKey;
    case 7:
        return &kPathTopology;
    case 8:
        return &kVertex;
    case 9:
        return &kEdge;
    case 10:
        return &kPath;
    case 11:
        return &kDateTime;
    case 12:
        return &kTime;
    case 13:
        return &kDuration;
    case 14:
        return &kList;
    case 15:
        return &kMap;
    default:
        return nullptr; // monostate / BytesValue: not classified here
    }
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> wrapRowsToChunkGenerator(std::vector<Row> rows) {
    if (rows.empty())
        co_return;

    size_t num_cols = 0;
    for (const auto& row : rows)
        num_cols = std::max(num_cols, row.size());
    if (num_cols == 0)
        co_return;

    // One column kind per position: first non-null cell wins. A position with no
    // typed cell anywhere stays ANY, matching the old RowBatch bridge.
    std::vector<binder::BoundTypeKind> kinds(num_cols, binder::BoundTypeKind::ANY);
    std::vector<bool> decided(num_cols, false);
    for (const auto& row : rows) {
        for (size_t c = 0; c < row.size(); ++c) {
            if (decided[c])
                continue;
            if (const auto* kind = kindOf(row[c])) {
                kinds[c] = *kind;
                decided[c] = true;
            }
        }
    }

    DataChunk chunk;
    for (size_t c = 0; c < num_cols; ++c)
        chunk.columns.push_back(Column::flat(kinds[c], rows.size()));

    for (size_t r = 0; r < rows.size(); ++r) {
        for (size_t c = 0; c < rows[r].size(); ++c)
            chunk.columns[c].setValue(r, rows[r][c]);
    }
    chunk.count = rows.size();
    chunk.sel = SelectionVector::identity(chunk.count);

    co_yield std::move(chunk);
}

} // namespace compute
} // namespace eugraph
