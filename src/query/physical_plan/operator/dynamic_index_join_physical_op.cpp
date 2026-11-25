#include "query/physical_plan/operator/dynamic_index_join_physical_op.hpp"

#include "query/dataset/row_identity.hpp"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace eugraph {
namespace compute {

namespace {

std::optional<PropertyValue> toIndexValue(const Value& value) {
    return std::visit(
        [](const auto& v) -> std::optional<PropertyValue> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return std::nullopt;
            else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int64_t> || std::is_same_v<T, double> ||
                               std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<uint8_t>>)
                return PropertyValue{v};
            else
                return std::nullopt;
        },
        value);
}

RowDigest digestOf(const Value& value) {
    RowDigest digest;
    uint8_t entity = 0;
    digest.cells.push_back(cellDigest(value, entity));
    digest.is_entity.push_back(entity);
    return digest;
}

bool isNullValue(const Value& value) {
    return std::holds_alternative<std::monostate>(value);
}

} // namespace

DynamicIndexJoinPhysicalOp::DynamicIndexJoinPhysicalOp(std::unique_ptr<PhysicalOperator> source,
                                                       std::unique_ptr<PhysicalOperator> index_chain,
                                                       IndexScanValuesPhysicalOp* index_scan, uint32_t source_key_col,
                                                       uint32_t dynamic_key_col, uint32_t match_key_col,
                                                       Schema source_schema, Schema match_schema,
                                                       std::vector<binder::BoundType> output_types,
                                                       Schema output_schema, size_t max_build_rows)
    : source_(std::move(source)), index_chain_(std::move(index_chain)), index_scan_(index_scan),
      source_key_col_(source_key_col), dynamic_key_col_(dynamic_key_col), match_key_col_(match_key_col),
      source_schema_(std::move(source_schema)), match_schema_(std::move(match_schema)),
      output_types_(std::move(output_types)), output_schema_(std::move(output_schema)),
      max_build_rows_(max_build_rows) {
    setOutputSchema(output_schema_, output_types_);
}

folly::coro::AsyncGenerator<DataChunk> DynamicIndexJoinPhysicalOp::executeChunk() {
    using Key = RowDigest;
    struct RowHash {
        size_t operator()(const Key& key) const noexcept {
            return RowDigestHash{}(key);
        }
    };

    std::unordered_map<Key, std::vector<std::vector<Value>>, RowHash> source_rows;
    std::unordered_set<Key, RowHash> seen_keys;
    std::vector<PropertyValue> index_values;
    size_t build_rows = 0;

    // Single pass over source: build join payload and collect distinct index keys.
    auto source_gen = source_->executeChunk();
    while (auto chunk = co_await source_gen.next()) {
        if (!chunk || chunk->count == 0)
            continue;
        for (size_t row = 0; row < chunk->count; ++row) {
            const Value join_value = chunk->getValue(source_key_col_, row);
            if (isNullValue(join_value))
                continue;
            if (max_build_rows_ > 0 && build_rows >= max_build_rows_)
                throw std::runtime_error("DynamicIndexJoin source exceeded memory row budget (" +
                                         std::to_string(max_build_rows_) + ")");
            std::vector<Value> payload;
            payload.reserve(source_schema_.size());
            for (size_t c = 0; c < source_schema_.size(); ++c)
                payload.push_back(chunk->getValue(c, row));
            source_rows[digestOf(join_value)].push_back(std::move(payload));
            ++build_rows;

            const Value dynamic_key = chunk->getValue(dynamic_key_col_, row);
            auto pv = toIndexValue(dynamic_key);
            if (!pv)
                continue;
            Key key = digestOf(dynamic_key);
            if (seen_keys.insert(std::move(key)).second)
                index_values.push_back(std::move(*pv));
        }
    }

    index_scan_->setValues(std::make_shared<std::vector<PropertyValue>>(std::move(index_values)));

    DataChunk output;
    for (size_t c = 0; c < output_types_.size(); ++c)
        output.columns.push_back(Column::flat(output_types_[c].kind));
    output.reserve(DataChunk::DEFAULT_CAPACITY);

    auto emit = [&](const std::vector<Value>& left, const DataChunk& right, size_t right_row) {
        for (size_t c = 0; c < left.size(); ++c)
            output.columns[c].setValue(output.count, left[c]);
        for (size_t c = 0; c < match_schema_.size(); ++c)
            output.columns[left.size() + c].setValue(output.count, right.getValue(c, right_row));
        ++output.count;
    };

    auto match_gen = index_chain_->executeChunk();
    while (auto chunk = co_await match_gen.next()) {
        if (!chunk || chunk->count == 0)
            continue;
        for (size_t row = 0; row < chunk->count; ++row) {
            const Value match_value = chunk->getValue(match_key_col_, row);
            if (isNullValue(match_value))
                continue;
            auto it = source_rows.find(digestOf(match_value));
            if (it == source_rows.end())
                continue;
            for (const auto& payload : it->second) {
                emit(payload, *chunk, row);
                if (output.count >= DataChunk::DEFAULT_CAPACITY) {
                    output.sel = SelectionVector::identity(output.count);
                    co_yield std::move(output);
                    output = DataChunk{};
                    for (size_t c = 0; c < output_types_.size(); ++c)
                        output.columns.push_back(Column::flat(output_types_[c].kind));
                    output.reserve(DataChunk::DEFAULT_CAPACITY);
                }
            }
        }
    }
    if (output.count > 0) {
        output.sel = SelectionVector::identity(output.count);
        co_yield std::move(output);
    }
}

void DynamicIndexJoinPhysicalOp::explain(ExplainNode& node, const ExplainContext& ctx) const {
    (void)ctx;
    node.operator_name = "DynamicIndexJoin";
    node.details.push_back("mode=dynamic-index-key");
    node.details.push_back("sourceKey=" + source_schema_[source_key_col_]);
    node.details.push_back("indexKey=" + source_schema_[dynamic_key_col_]);
    node.details.push_back("matchKey=" + match_schema_[match_key_col_]);
    if (max_build_rows_ > 0)
        node.details.push_back("max_build_rows=" + std::to_string(max_build_rows_));
}

} // namespace compute
} // namespace eugraph
