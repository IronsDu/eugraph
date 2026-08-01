#pragma once

#include "query/dataset/data_chunk.hpp"

#include <utility>

namespace eugraph {
namespace compute {

/// 把列物化为独占的 FLAT buffer。
///
/// Column 内部 buffer 是 std::shared_ptr<ColumnBuffer>。DICTIONARY 列通过
/// Column::dict(buf, sel) 共享上游 FLAT 列的 buffer 实现零拷贝派生。
/// 直接对共享 buffer 调用 setValue 会写穿透到所有引用该 buffer 的列。
///
/// mutation 算子在写回 VertexValue / EdgeValue 之前必须调用本函数：
/// 1. 非 FLAT 形式（CONSTANT / DICTIONARY）→ 物化为新 FLAT 列；
/// 2. FLAT 但 buffer.use_count() > 1 → COW 拷贝；
/// 3. FLAT 且独占 → no-op。
inline void ensureExclusiveBuffer(Column& col, size_t n) {
    auto materialize = [&]() {
        auto fresh = Column::flat(col.type, n);
        for (size_t i = 0; i < n; ++i)
            fresh.setValue(i, col.getValue(i));
        col = std::move(fresh);
    };

    if (col.form != VectorForm::FLAT) {
        materialize();
        return;
    }
    if (col.buffer && col.buffer.use_count() > 1) {
        materialize();
    }
}

/// 把 chunk 中 (trigger_col, row) 处的 vertex 替换为 updated，
/// 并扫描同一 row 的其他 VERTEX 列，对 vertex.id 相同的列一并替换。
///
/// 场景：MATCH (a) WITH a AS a1, a AS a2 SET a1.p = 1 RETURN a2.p
/// 同一 vid 在 chunk 中可能被多列引用（值语义独立拷贝），只更新触发列
/// 会让别名列读到旧值。
inline void mirrorVertexToAllReferences(DataChunk& chunk, size_t trigger_col, size_t row, VertexValue updated) {
    ensureExclusiveBuffer(chunk.columns[trigger_col], chunk.count);
    chunk.columns[trigger_col].setValue(row, Value(updated));

    for (size_t c = 0; c < chunk.columns.size(); ++c) {
        if (c == trigger_col)
            continue;
        auto& col = chunk.columns[c];
        if (col.type != binder::BoundTypeKind::VERTEX)
            continue;
        Value v = col.getValue(row);
        if (!std::holds_alternative<VertexValue>(v))
            continue;
        const auto& vv = std::get<VertexValue>(v);
        if (vv.id != updated.id)
            continue;
        ensureExclusiveBuffer(col, chunk.count);
        col.setValue(row, Value(updated));
    }
}

/// 把 chunk 中 (trigger_col, row) 处的 edge 替换为 updated，
/// 并扫描同一 row 的其他 EDGE 列，对 edge.id 相同的列一并替换。
inline void mirrorEdgeToAllReferences(DataChunk& chunk, size_t trigger_col, size_t row, EdgeValue updated) {
    ensureExclusiveBuffer(chunk.columns[trigger_col], chunk.count);
    chunk.columns[trigger_col].setValue(row, Value(updated));

    for (size_t c = 0; c < chunk.columns.size(); ++c) {
        if (c == trigger_col)
            continue;
        auto& col = chunk.columns[c];
        if (col.type != binder::BoundTypeKind::EDGE)
            continue;
        Value v = col.getValue(row);
        if (!std::holds_alternative<EdgeValue>(v))
            continue;
        const auto& ev = std::get<EdgeValue>(v);
        if (ev.id != updated.id)
            continue;
        ensureExclusiveBuffer(col, chunk.count);
        col.setValue(row, Value(updated));
    }
}

} // namespace compute
} // namespace eugraph
