#include "query/physical_plan/operator/hash_join_physical_op.hpp"

namespace eugraph {
namespace compute {

namespace {
using Key = std::vector<Value>;

// 1 = vertex-like, 2 = edge-like. VertexValue/VertexRef (and EdgeValue/
// EdgeKey) denote the same graph entity with the same identity even when one
// side was materialized and the other is still a topology reference.
int graphEntityKind(const Value& value) {
    if (std::holds_alternative<VertexRef>(value) || std::holds_alternative<VertexValue>(value))
        return 1;
    if (std::holds_alternative<EdgeKey>(value) || std::holds_alternative<EdgeValue>(value))
        return 2;
    return 0;
}

/// Identity of a graph entity, whichever representation it arrived in.
uint64_t entityId(const Value& value) {
    if (std::holds_alternative<VertexRef>(value))
        return std::get<VertexRef>(value).id;
    if (std::holds_alternative<VertexValue>(value))
        return std::get<VertexValue>(value).id;
    if (std::holds_alternative<EdgeKey>(value))
        return std::get<EdgeKey>(value).id;
    return std::get<EdgeValue>(value).id;
}

bool joinValueEquals(const Value& a, const Value& b) {
    int kind_a = graphEntityKind(a);
    int kind_b = graphEntityKind(b);
    if (kind_a != 0 && kind_a == kind_b)
        return entityId(a) == entityId(b);
    auto eq = valueEquals(a, b);
    return eq && *eq;
}

/// Hash of one key element, normalised the same way joinValueEquals compares.
///
/// The two must agree: an unordered_map requires that equal keys hash equally.
/// Hashing the raw Value broke that for entities -- VertexRef{id} and
/// VertexValue{id} compare equal but landed in different buckets, so a matching
/// pair could be silently missed rather than merely costing a comparison. This
/// path is reached whenever one side is still a topology reference while the
/// other has been materialised, which is the normal state after a projection
/// extract.
size_t joinValueHash(const Value& value) {
    if (graphEntityKind(value) != 0)
        return std::hash<uint64_t>{}(entityId(value));
    return ValueHash{}(value);
}

struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
        size_t h = 0x9e3779b9;
        for (const auto& v : k)
            h ^= joinValueHash(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
struct KeyEq {
    bool operator()(const Key& a, const Key& b) const noexcept {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (!joinValueEquals(a[i], b[i]))
                return false;
        }
        return true;
    }
};
} // namespace

folly::coro::AsyncGenerator<DataChunk> HashJoinPhysicalOp::executeChunk() {
    // Build phase: materialize all right rows.
    std::unordered_map<Key, std::vector<std::vector<Value>>, KeyHash, KeyEq> hash_table;
    size_t right_cols = 0;
    {
        auto right_gen = cancellable(right_->executeChunk());
        while (auto chunk = co_await right_gen.next()) {
            if (!chunk || chunk->count == 0)
                continue;
            right_cols = chunk->numColumns();
            for (size_t row = 0; row < chunk->count; ++row) {
                Key key;
                key.reserve(right_key_cols_.size());
                for (uint32_t col : right_key_cols_)
                    key.push_back(chunk->getValue(col, row));
                std::vector<Value> vals;
                vals.reserve(right_cols);
                for (size_t c = 0; c < right_cols; ++c)
                    vals.push_back(chunk->getValue(c, row));
                hash_table[std::move(key)].push_back(std::move(vals));
            }
        }
    }

    // Probe phase.
    auto left_gen = cancellable(left_->executeChunk());
    while (auto left_chunk = co_await left_gen.next()) {
        if (!left_chunk || left_chunk->count == 0)
            continue;
        const size_t left_cols = left_chunk->numColumns();

        DataChunk output;
        output.columns.reserve(left_cols + right_cols);
        for (size_t c = 0; c < left_cols; ++c)
            output.columns.push_back(Column::flat(left_chunk->columns[c].type));
        for (size_t c = 0; c < right_cols; ++c)
            output.columns.push_back(Column::flat(output_types_[left_cols + c].kind));
        output.reserve(DataChunk::DEFAULT_CAPACITY);

        for (size_t row = 0; row < left_chunk->count; ++row) {
            Key key;
            key.reserve(left_key_cols_.size());
            for (uint32_t col : left_key_cols_)
                key.push_back(left_chunk->getValue(col, row));
            auto it = hash_table.find(key);
            if (it == hash_table.end())
                continue;
            for (const auto& right_vals : it->second) {
                for (size_t c = 0; c < left_cols; ++c)
                    output.columns[c].setValue(output.count, left_chunk->getValue(c, row));
                for (size_t c = 0; c < right_cols; ++c)
                    output.columns[left_cols + c].setValue(output.count, right_vals[c]);
                output.count++;
                if (output.count >= DataChunk::DEFAULT_CAPACITY) {
                    output.sel = SelectionVector::identity(output.count);
                    co_yield std::move(output);
                    output = DataChunk();
                    output.columns.reserve(left_cols + right_cols);
                    for (size_t c = 0; c < left_cols; ++c)
                        output.columns.push_back(Column::flat(left_chunk->columns[c].type));
                    for (size_t c = 0; c < right_cols; ++c)
                        output.columns.push_back(Column::flat(output_types_[left_cols + c].kind));
                    output.reserve(DataChunk::DEFAULT_CAPACITY);
                }
            }
        }
        if (output.count > 0) {
            output.sel = SelectionVector::identity(output.count);
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
