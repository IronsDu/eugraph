#include "compute_service/physical_plan/operator/set_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/vectorized_evaluator.hpp"

namespace eugraph {
namespace compute {

namespace {

PropertyValue valueToPropertyValue(const Value& v) {
    if (std::holds_alternative<std::monostate>(v))
        return PropertyValue{};
    if (std::holds_alternative<bool>(v))
        return PropertyValue(std::get<bool>(v));
    if (std::holds_alternative<int64_t>(v))
        return PropertyValue(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))
        return PropertyValue(std::get<double>(v));
    if (std::holds_alternative<std::string>(v))
        return PropertyValue(std::get<std::string>(v));
    return PropertyValue{};
}

int findColumn(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

} // anonymous namespace

folly::coro::AsyncGenerator<DataChunk> SetPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        // Pre-evaluate all value expressions for this chunk.
        std::vector<std::vector<Value>> value_results(items_.size());
        VectorizedEvaluator evaluator;
        for (size_t idx = 0; idx < items_.size(); ++idx) {
            if (items_[idx].value.has_value()) {
                auto col = Column::flat(binder::BoundTypeKind::ANY, n);
                evaluator.evaluate(*items_[idx].value, *chunk, col);
                value_results[idx].reserve(n);
                for (size_t r = 0; r < n; ++r) {
                    value_results[idx].push_back(col.getValue(r));
                }
            }
        }

        for (size_t row_idx = 0; row_idx < n; ++row_idx) {
            for (size_t idx = 0; idx < items_.size(); ++idx) {
                const auto& item = items_[idx];
                if (item.var_name.empty())
                    continue;

                int col = findColumn(input_schema_, item.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;

                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);
                if (!std::holds_alternative<VertexValue>(val))
                    continue;

                const auto& vertex = std::get<VertexValue>(val);
                VertexId vid = vertex.id;

                if (item.kind == cypher::SetItemKind::SET_LABELS) {
                    auto lit = label_name_to_id_.find(item.label);
                    if (lit == label_name_to_id_.end())
                        continue;
                    co_await store_.addVertexLabel(vid, lit->second);
                } else if (item.kind == cypher::SetItemKind::SET_PROPERTY) {
                    if (item.prop_name.empty())
                        continue;

                    LabelId found_label = INVALID_LABEL_ID;
                    uint16_t found_prop_id = 0;
                    size_t match_count = 0;
                    for (const auto& [lid, ldef] : label_defs_) {
                        for (const auto& pd : ldef.properties) {
                            if (pd.name == item.prop_name) {
                                found_label = lid;
                                found_prop_id = pd.id;
                                ++match_count;
                            }
                        }
                    }
                    if (match_count == 0 || match_count > 1)
                        continue;
                    if (!item.value.has_value())
                        continue;

                    Value v = value_results[idx][row_idx];
                    PropertyValue pv = valueToPropertyValue(v);
                    co_await store_.putVertexProperty(vid, found_label, found_prop_id, pv);
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
