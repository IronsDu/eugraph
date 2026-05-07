#include "compute_service/physical_plan/operator/remove_physical_op.hpp"
#include "common/types/graph_types.hpp"

namespace eugraph {
namespace compute {

namespace {

std::string extractVariableName(const cypher::Expression& target) {
    if (std::holds_alternative<std::unique_ptr<cypher::Variable>>(target)) {
        return std::get<std::unique_ptr<cypher::Variable>>(target)->name;
    }
    if (std::holds_alternative<std::unique_ptr<cypher::PropertyAccess>>(target)) {
        auto& pa = std::get<std::unique_ptr<cypher::PropertyAccess>>(target);
        return extractVariableName(pa->object);
    }
    if (std::holds_alternative<std::unique_ptr<cypher::LabelCastExpr>>(target)) {
        auto& lc = std::get<std::unique_ptr<cypher::LabelCastExpr>>(target);
        return extractVariableName(lc->object);
    }
    return {};
}

int findColumn(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

} // anonymous namespace

folly::coro::AsyncGenerator<DataChunk> RemovePhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();
        for (size_t row_idx = 0; row_idx < n; ++row_idx) {
            for (const auto& item : items_) {
                std::string var_name = extractVariableName(item.target);
                if (var_name.empty()) continue;

                int col = findColumn(input_schema_, var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns()) continue;

                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);
                if (!std::holds_alternative<VertexValue>(val)) continue;

                const auto& vertex = std::get<VertexValue>(val);
                VertexId vid = vertex.id;

                if (item.kind == cypher::RemoveItem::Kind::LABEL) {
                    auto lit = label_name_to_id_.find(item.name);
                    if (lit == label_name_to_id_.end()) continue;
                    co_await store_.removeVertexLabel(vid, lit->second);
                } else if (item.kind == cypher::RemoveItem::Kind::PROPERTY) {
                    LabelId found_label = INVALID_LABEL_ID;
                    uint16_t found_prop_id = 0;
                    size_t match_count = 0;
                    for (const auto& [lid, ldef] : label_defs_) {
                        for (const auto& pd : ldef.properties) {
                            if (pd.name == item.name) {
                                found_label = lid;
                                found_prop_id = pd.id;
                                ++match_count;
                            }
                        }
                    }
                    if (match_count == 0 || match_count > 1) continue;
                    co_await store_.deleteVertexProperty(vid, found_label, found_prop_id);
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
