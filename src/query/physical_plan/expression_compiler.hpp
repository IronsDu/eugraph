#pragma once

#include "query/physical_plan/slot_layout.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_expression/bound_quantifier_expr.hpp"

namespace eugraph {
namespace compute {

/// Compiles bound expressions by resolving every BoundColumnRef's
/// slot_id to a physical column_index, caching the result in the
/// expression's column_index field.
///
/// Called once per physical operator during init() (compile phase).
/// After compilation, the evaluator reads column_index directly
/// without any SlotId lookup.
class ExpressionCompiler {
public:
    explicit ExpressionCompiler(const TupleSlotLayout& layout) : layout_(layout) {}

    /// Compile a single expression (inline replacement of slot_id →
    /// column_index).
    void compile(binder::BoundExpression& expr) {
        std::visit([this](auto& val) { compileOne(val); }, expr);
    }

private:
    template <typename T> void compileOne(T& val) {
        using Type = std::decay_t<T>;
        if constexpr (std::is_same_v<Type, binder::BoundColumnRef>) {
            int idx = layout_.getColumnIndex(val.slot_id);
            if (idx >= 0)
                val.column_index = static_cast<uint32_t>(idx);
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundBinaryOp>>) {
            if (val) {
                compile(val->left);
                compile(val->right);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundUnaryOp>>) {
            if (val)
                compile(val->operand);
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundFunctionCall>>) {
            if (val) {
                for (auto& arg : val->args)
                    compile(arg);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundPropertyRef>>) {
            if (val) {
                // Update the inner BoundColumnRef's column_index
                compile(val->object);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
            if (val)
                compile(val->object);
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundLabelCast>>) {
            if (val)
                compile(val->object);
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundSubscript>>) {
            if (val) {
                compile(val->list);
                compile(val->index);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundCase>>) {
            if (val) {
                if (val->subject)
                    compile(*val->subject);
                for (auto& [w, t] : val->when_thens) {
                    compile(w);
                    compile(t);
                }
                if (val->else_expr)
                    compile(*val->else_expr);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundAllExpr>> ||
                             std::is_same_v<Type, std::unique_ptr<binder::BoundAnyExpr>> ||
                             std::is_same_v<Type, std::unique_ptr<binder::BoundNoneExpr>> ||
                             std::is_same_v<Type, std::unique_ptr<binder::BoundSingleExpr>>) {
            // Quantifier expressions: compile list_expr and where_pred so
            // outer-variable BoundColumnRefs resolve to runtime column index
            // (PE may have appended columns after the binder assigned column
            // indices). The loop variable's own BoundColumnRef has a slot_id
            // absent from the outer layout — getColumnIndex returns -1 and
            // the bind-time column_index (= loop_column_index) is preserved.
            if (val) {
                compile(val->list_expr);
                if (val->where_pred)
                    compile(*val->where_pred);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundListComprehension>>) {
            if (val) {
                compile(val->list_expr);
                if (val->where_pred)
                    compile(*val->where_pred);
                compile(val->projection);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundList>>) {
            if (val) {
                for (auto& elem : val->elements)
                    compile(elem);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundMap>>) {
            if (val) {
                for (auto& [key, val_ent] : val->entries)
                    compile(val_ent);
            }
        } else if constexpr (std::is_same_v<Type, std::unique_ptr<binder::BoundSlice>>) {
            if (val) {
                compile(val->list);
                if (val->from)
                    compile(*val->from);
                if (val->to)
                    compile(*val->to);
            }
        } else {
            // BoundLiteral, BoundVariableRef, BoundParameter — no slot
            // resolution needed.
        }
    }

    const TupleSlotLayout& layout_;
};

} // namespace compute
} // namespace eugraph
