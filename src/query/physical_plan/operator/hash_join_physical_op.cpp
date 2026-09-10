#include "query/physical_plan/operator/hash_join_physical_op.hpp"

namespace eugraph {
namespace compute {

namespace {
using Key = std::vector<Value>;
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
            auto eq = valueEquals(a[i], b[i]);
            if (!eq || !*eq)
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
        auto right_gen = right_->executeChunk();
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
    auto left_gen = left_->executeChunk();
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
