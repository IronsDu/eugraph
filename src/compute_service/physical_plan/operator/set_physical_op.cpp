#include "compute_service/physical_plan/operator/set_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

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

folly::coro::AsyncGenerator<RowBatch> SetPhysicalOp::execute() {
    auto child_gen = child_->execute();

    while (auto child_batch = co_await child_gen.next()) {
        RowBatch output;
        for (auto& row : child_batch->rows) {
            for (const auto& item : items_) {
                std::string var_name = extractVariableName(item.target);
                if (var_name.empty())
                    continue;

                int col = findColumn(input_schema_, var_name);
                if (col < 0 || static_cast<size_t>(col) >= row.size())
                    continue;
                if (!std::holds_alternative<VertexValue>(row[col]))
                    continue;

                const auto& vertex = std::get<VertexValue>(row[col]);
                VertexId vid = vertex.id;

                if (item.kind == cypher::SetItemKind::SET_LABELS) {
                    if (!label_name_to_id_)
                        continue;
                    auto lit = label_name_to_id_->find(item.label);
                    if (lit == label_name_to_id_->end())
                        continue;
                    co_await store_.addVertexLabel(vid, lit->second);
                } else if (item.kind == cypher::SetItemKind::SET_PROPERTY) {
                    if (!label_defs_)
                        continue;
                    std::string prop_name;
                    if (std::holds_alternative<std::unique_ptr<cypher::PropertyAccess>>(item.target)) {
                        prop_name = std::get<std::unique_ptr<cypher::PropertyAccess>>(item.target)->property;
                    }
                    if (prop_name.empty())
                        continue;

                    LabelId found_label = INVALID_LABEL_ID;
                    uint16_t found_prop_id = 0;
                    size_t match_count = 0;
                    for (const auto& [lid, ldef] : *label_defs_) {
                        for (const auto& pd : ldef.properties) {
                            if (pd.name == prop_name) {
                                found_label = lid;
                                found_prop_id = pd.id;
                                ++match_count;
                            }
                        }
                    }
                    if (match_count == 0)
                        continue;
                    if (match_count > 1)
                        continue;

                    if (!item.value.has_value())
                        continue;
                    ExpressionEvaluator eval;
                    eval.setLabelDefs(label_defs_);
                    Value val = eval.evaluate(*item.value, row, input_schema_);
                    PropertyValue pv = valueToPropertyValue(val);

                    co_await store_.putVertexProperty(vid, found_label, found_prop_id, pv);
                }
            }
            output.push_back(std::move(row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
