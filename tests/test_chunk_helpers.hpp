#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"

#include <vector>

namespace eugraph {
namespace test {

/// Materialise a chunk as plain rows for assertions.
///
/// `DataChunk::toRows()` was removed from the production API: it allocated a
/// std::vector<Value> per row and copied every cell, while its production call
/// sites each read a single column or serialised each row exactly once. Tests
/// still want whole rows to assert against, so the helper lives here.
inline std::vector<Row> chunkToRows(const DataChunk& chunk) {
    std::vector<Row> rows;
    const size_t n = chunk.numRows();
    rows.reserve(n);
    for (size_t r = 0; r < n; ++r) {
        Row row;
        row.reserve(chunk.numColumns());
        for (size_t c = 0; c < chunk.numColumns(); ++c)
            row.push_back(chunk.columns[c].getValue(r));
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace test
} // namespace eugraph
