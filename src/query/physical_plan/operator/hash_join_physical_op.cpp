#include "query/physical_plan/operator/hash_join_physical_op.hpp"

#include <stdexcept>

namespace eugraph {
namespace compute {

namespace {
using Key = std::vector<Value>;

bool hasNullKey(const Key& key) {
    for (const auto& value : key) {
        if (std::holds_alternative<std::monostate>(value))
            return true;
    }
    return false;
}

// 1 = vertex-like, 2 = edge-like. VertexValue/VertexRef (and EdgeValue/
// EdgeKey) denote the same graph entity with the same identity even when one
// side was materialized and the other is still a topology reference.
int graphEntityKind(const Value& value) {
    if (std::holds_alternative<VertexRef>(value) || std::holds_alternative<VertexValuePtr>(value))
        return 1;
    if (std::holds_alternative<EdgeKey>(value) || std::holds_alternative<EdgeValuePtr>(value))
        return 2;
    return 0;
}

bool joinValueEquals(const Value& a, const Value& b) {
    int kind_a = graphEntityKind(a);
    int kind_b = graphEntityKind(b);
    if (kind_a != 0 && kind_a == kind_b) {
        auto entityId = [](const Value& value) -> uint64_t {
            if (std::holds_alternative<VertexRef>(value))
                return std::get<VertexRef>(value).id;
            if (std::holds_alternative<VertexValuePtr>(value))
                return (*std::get<VertexValuePtr>(value)).id;
            if (std::holds_alternative<EdgeKey>(value))
                return std::get<EdgeKey>(value).id;
            return (*std::get<EdgeValuePtr>(value)).id;
        };
        return entityId(a) == entityId(b);
    }
    auto eq = valueEquals(a, b);
    return eq && *eq;
}

struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
        size_t h = 0x9e3779b9;
        for (const auto& v : k)
            h ^= ValueHash{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
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
    // The build side is materialized; the other side is streamed. The planner
    // chooses the smaller side as build and passes an upper bound so a bad
    // estimate cannot silently consume all memory.
    auto& build = build_left_ ? *left_ : *right_;
    auto& probe = build_left_ ? *right_ : *left_;
    const auto& build_key_cols = build_left_ ? left_key_cols_ : right_key_cols_;
    const auto& probe_key_cols = build_left_ ? right_key_cols_ : left_key_cols_;

    std::unordered_map<Key, std::vector<std::vector<Value>>, KeyHash, KeyEq> hash_table;
    size_t build_rows = 0;
    {
        auto build_gen = cancellable(build.executeChunkTraced());
        while (auto chunk = co_await build_gen.next()) {
            if (!chunk || chunk->count == 0)
                continue;
            const size_t build_cols = chunk->numColumns();
            for (size_t row = 0; row < chunk->count; ++row) {
                if (max_build_rows_ > 0 && build_rows >= max_build_rows_) {
                    throw std::runtime_error("HashJoin build side exceeded memory row budget (" +
                                             std::to_string(max_build_rows_) + "); planner estimate was too small");
                }
                Key key;
                key.reserve(build_key_cols.size());
                for (uint32_t col : build_key_cols)
                    key.push_back(chunk->getValue(col, row));
                // Cypher: null = null is null, not true; null keys never join.
                if (hasNullKey(key))
                    continue;
                std::vector<Value> vals;
                vals.reserve(build_cols);
                for (size_t c = 0; c < build_cols; ++c)
                    vals.push_back(chunk->getValue(c, row));
                hash_table[std::move(key)].push_back(std::move(vals));
                ++build_rows;
            }
        }
    }

    const size_t left_cols = left_->outputSchema().size();
    const size_t right_cols = right_->outputSchema().size();
    const size_t total_cols = left_cols + right_cols;
    if (output_types_.size() != total_cols) {
        throw std::runtime_error("HashJoin output type count does not match child schemas");
    }

    auto make_output = [&] {
        DataChunk output;
        output.columns.reserve(total_cols);
        for (size_t c = 0; c < total_cols; ++c)
            output.columns.push_back(Column::flat(output_types_[c].kind));
        output.reserve(DataChunk::DEFAULT_CAPACITY);
        return output;
    };

    auto probe_gen = cancellable(probe.executeChunkTraced());
    while (auto probe_chunk = co_await probe_gen.next()) {
        if (!probe_chunk || probe_chunk->count == 0)
            continue;
        const size_t probe_cols = probe_chunk->numColumns();
        DataChunk output = make_output();

        for (size_t row = 0; row < probe_chunk->count; ++row) {
            Key key;
            key.reserve(probe_key_cols.size());
            for (uint32_t col : probe_key_cols)
                key.push_back(probe_chunk->getValue(col, row));
            if (hasNullKey(key))
                continue;
            auto it = hash_table.find(key);
            if (it == hash_table.end())
                continue;

            for (const auto& build_vals : it->second) {
                const size_t left_count = build_left_ ? build_vals.size() : probe_cols;
                const size_t right_count = build_left_ ? probe_cols : build_vals.size();
                if (left_count != left_cols || right_count != right_cols)
                    throw std::runtime_error("HashJoin child schema mismatch");
                for (size_t c = 0; c < left_cols; ++c) {
                    output.columns[c].setValue(output.count,
                                               build_left_ ? build_vals[c] : probe_chunk->getValue(c, row));
                }
                for (size_t c = 0; c < right_cols; ++c) {
                    output.columns[left_cols + c].setValue(output.count,
                                                           build_left_ ? probe_chunk->getValue(c, row) : build_vals[c]);
                }
                output.count++;
                if (output.count >= DataChunk::DEFAULT_CAPACITY) {
                    output.sel = SelectionVector::identity(output.count);
                    co_yield std::move(output);
                    output = make_output();
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
