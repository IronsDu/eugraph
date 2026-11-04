#include "query/physical_plan/operator/aggregate_physical_op.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/expression_evaluator.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> AggregatePhysicalOp::executeChunk() {
    // Per-group state: one AggStateBase* per aggregate expression.
    struct GroupState {
        std::vector<std::unique_ptr<function::AggStateBase>> agg_states;
        // Per-aggregate distinct sets (only used when aggregate.distinct == true).
        std::vector<std::unordered_set<Value, ValueHash, ValueContentEqual>> distinct_sets;
    };

    // Aggregate arguments are evaluated once per chunk into typed Columns.
    // arg_views is the stable pointer view handed to AggUpdateFn; it is built
    // after args is complete and never mutated again.
    struct EvaluatedAggregate {
        std::vector<Column> args;
        std::vector<const Column*> arg_views;
    };

    std::unordered_map<Row, GroupState, RowHash, RowEqual> groups;
    std::vector<Row> group_order;
    // Pointers into the node-based unordered_map stay valid across rehash;
    // output does not need to re-hash/re-compare nested list/path keys.
    std::vector<GroupState*> group_states;

    const bool has_aggregates = !aggregates_.empty();

    auto child_gen = cancellable(child_->executeChunkTraced());
    ExpressionEvaluator evaluator(eval_ctx_);

    std::vector<const binder::BoundExpression*> prefetch_exprs;
    prefetch_exprs.reserve(group_keys_.size());
    for (const auto& key : group_keys_)
        prefetch_exprs.push_back(&key.expr);
    for (const auto& agg : aggregates_) {
        for (const auto& arg : agg.arguments)
            prefetch_exprs.push_back(&arg);
    }

    // A variadic aggregate with a literal NULL first argument (count(*),
    // collect(null), ...) must not have that null filtered out. The aggregate
    // still receives the literal-null column; this flag only controls the
    // operator's null-skipping policy, exactly as in the old implementation.
    std::vector<bool> literal_null_first_arg(aggregates_.size(), false);
    for (size_t i = 0; i < aggregates_.size(); ++i) {
        const auto& agg = aggregates_[i];
        literal_null_first_arg[i] = agg.func_def && agg.func_def->has_variadic_args && !agg.arguments.empty() &&
                                    std::holds_alternative<binder::BoundLiteral>(agg.arguments[0]) &&
                                    isNull(std::get<binder::BoundLiteral>(agg.arguments[0]).value);
    }

    // Only sum/avg may safely coerce numeric arguments into a typed buffer:
    // their arithmetic is numeric anyway. min/max/collect/percentile/count must
    // preserve the runtime Value variant (mixed int/double, NULL, entity kinds),
    // otherwise e.g. max([1, 2.0, 5]) returns 5.0 instead of 5.
    std::vector<bool> typed_numeric_args(aggregates_.size(), false);
    for (size_t i = 0; i < aggregates_.size(); ++i) {
        const auto& agg = aggregates_[i];
        if (!agg.func_def)
            continue;
        if (agg.func_def->name != "sum" && agg.func_def->name != "avg")
            continue;
        typed_numeric_args[i] = std::all_of(agg.arguments.begin(), agg.arguments.end(), [](const auto& arg) {
            const auto kind = binder::getBoundExprType(arg).kind;
            return kind == binder::BoundTypeKind::INT64 || kind == binder::BoundTypeKind::DOUBLE;
        });
    }

    while (auto chunk = co_await child_gen.next()) {
        const size_t n = chunk->numRows();
        if (n == 0)
            continue;

        evaluator.resetForChunk();
        co_await evaluator.prefetch(prefetch_exprs, *chunk);

        // Group keys keep the old ANY storage: Row still owns the Value used by
        // the hash map, so this step does not need a typed representation.
        std::vector<Column> key_columns;
        key_columns.reserve(group_keys_.size());
        for (const auto& key : group_keys_) {
            Column col = Column::flat(binder::BoundTypeKind::ANY, n);
            evaluator.evaluate(key.expr, *chunk, col);
            key_columns.push_back(std::move(col));
        }

        std::vector<EvaluatedAggregate> evaluated(aggregates_.size());
        for (size_t a = 0; a < aggregates_.size(); ++a) {
            const auto& agg = aggregates_[a];
            if (!agg.func_def)
                continue; // Complex aggregate: handled during output substitution.

            auto& plan = evaluated[a];
            plan.args.reserve(agg.arguments.size());
            plan.arg_views.reserve(agg.arguments.size());
            for (const auto& arg : agg.arguments) {
                // Only use a typed column when the expression's stage cannot
                // change representation. Graph entities can arrive as VERTEX_REF
                // at the topology stage or VERTEX after enrichment; forcing a
                // VERTEX column would silently drop a VERTEX_REF value (validity
                // bit set, payload null), which breaks count(DISTINCT m).
                // ANY keeps the old, representation-agnostic semantics for those
                // kinds while numeric/string aggregates still get typed buffers.
                const auto arg_kind = binder::getBoundExprType(arg).kind;
                const auto storage_kind = (typed_numeric_args[a] && (arg_kind == binder::BoundTypeKind::INT64 ||
                                                                     arg_kind == binder::BoundTypeKind::DOUBLE))
                                              ? arg_kind
                                              : binder::BoundTypeKind::ANY;
                Column col = Column::flat(storage_kind, n);
                evaluator.evaluate(arg, *chunk, col);
                plan.args.push_back(std::move(col));
            }
            for (const auto& col : plan.args)
                plan.arg_views.push_back(&col);
        }

        for (size_t r = 0; r < n; ++r) {
            Row key;
            key.reserve(group_keys_.size());
            for (const auto& col : key_columns)
                key.push_back(col.getValue(r));

            auto it = groups.find(key);
            if (it == groups.end()) {
                GroupState gs;
                gs.agg_states.reserve(aggregates_.size());
                gs.distinct_sets.resize(aggregates_.size());
                for (const auto& agg : aggregates_) {
                    if (agg.func_def && agg.func_def->agg_init)
                        gs.agg_states.push_back(agg.func_def->agg_init());
                    else
                        gs.agg_states.push_back(nullptr);
                }
                it = groups.emplace(std::move(key), std::move(gs)).first;
                group_order.push_back(it->first);
                group_states.push_back(&it->second);
            }

            auto& state = it->second;
            for (size_t i = 0; i < aggregates_.size(); ++i) {
                const auto& agg = aggregates_[i];
                if (!agg.func_def || !agg.func_def->agg_update || !state.agg_states[i])
                    continue;

                const auto& plan = evaluated[i];
                // Null policy is exactly the old one: count(*) / a variadic
                // literal-null argument never skips; collect keeps nulls; all
                // other aggregates skip a null first argument. DISTINCT always
                // skips nulls because the skip condition is evaluated first.
                if (!literal_null_first_arg[i] && !plan.args.empty() && plan.args[0].isNull(r) &&
                    !(agg.keeps_nulls && !agg.distinct))
                    continue;

                // DISTINCT dedup is keyed on the first argument. This is the
                // only place that still needs a Value for the argument; the
                // aggregate update itself receives the typed column view.
                if (agg.distinct) {
                    Value dedup_key = plan.args.empty() ? Value{} : plan.args[0].getValue(r);
                    if (!state.distinct_sets[i].insert(std::move(dedup_key)).second)
                        continue;
                }

                function::AggregateInput input{
                    std::span<const Column* const>(plan.arg_views.data(), plan.arg_views.size()), r};
                agg.func_def->agg_update(*state.agg_states[i], input);
            }
        }
    }

    // Build output FLAT DataChunk.
    size_t visible_aggs = 0;
    for (const auto& a : aggregates_)
        if (!a.is_internal)
            ++visible_aggs;
    size_t out_cols = group_keys_.size() + visible_aggs;

    auto buildOutputChunk = [&]() -> DataChunk {
        DataChunk dc;
        for (size_t c = 0; c < out_cols; ++c) {
            auto kind = (c < output_type_kinds_.size()) ? output_type_kinds_[c] : binder::BoundTypeKind::ANY;
            dc.columns.push_back(Column::flat(kind, DataChunk::DEFAULT_CAPACITY));
        }
        return dc;
    };

    DataChunk output = buildOutputChunk();
    size_t row_idx = 0;

    for (size_t gi = 0; gi < group_order.size(); ++gi) {
        const auto& key = group_order[gi];
        auto& state = *group_states[gi];

        for (size_t c = 0; c < key.size(); ++c) {
            output.columns[c].setValue(row_idx, key[c]);
        }

        size_t visible_col = group_keys_.size();
        for (size_t i = 0; i < aggregates_.size(); ++i) {
            const auto& agg = aggregates_[i];
            if (agg.is_internal) {
                // Internal aggregates are accumulated but not output.
                // They serve as inputs to complex aggregate expressions.
                continue;
            }
            size_t col_idx = visible_col++;

            if (agg.func_def && agg.func_def->agg_finalize && state.agg_states[i]) {
                output.columns[col_idx].setValue(row_idx, agg.func_def->agg_finalize(*state.agg_states[i]));
            } else if (!agg.func_def) {
                // Complex aggregate: find internal aggregates that provide inner results,
                // build substitution map, then evaluate the full expression.
                std::unordered_map<const function::FunctionDef*, Value> subs;
                for (size_t j = 0; j < aggregates_.size(); ++j) {
                    if (aggregates_[j].is_internal && aggregates_[j].func_def && state.agg_states[j] &&
                        aggregates_[j].func_def->agg_finalize)
                        subs[aggregates_[j].func_def] = aggregates_[j].func_def->agg_finalize(*state.agg_states[j]);
                }
                DataChunk eval_chunk;
                eval_chunk.count = 1;
                ExpressionEvaluator out_eval(eval_ctx_);
                out_eval.aggregate_substitutions = &subs;
                auto result_col = Column::flat(binder::BoundTypeKind::ANY, 1);
                out_eval.evaluate(agg.arguments[0], eval_chunk, result_col);
                output.columns[col_idx].setValue(row_idx, result_col.getValue(0));
            } else {
                output.columns[col_idx].setNull(row_idx);
            }
        }

        ++row_idx;
        ++output.count;

        if (row_idx >= DataChunk::DEFAULT_CAPACITY) {
            co_yield std::move(output);
            output = buildOutputChunk();
            row_idx = 0;
        }
    }

    // Empty input with aggregates but no group keys → yield one row with default values.
    if (group_keys_.empty() && group_order.empty() && has_aggregates) {
        size_t out_idx = 0;
        for (size_t i = 0; i < aggregates_.size(); ++i) {
            if (aggregates_[i].is_internal)
                continue;
            const auto& agg = aggregates_[i];
            if (agg.func_def && agg.func_def->agg_init && agg.func_def->agg_finalize) {
                auto init_state = agg.func_def->agg_init();
                output.columns[out_idx].setValue(0, agg.func_def->agg_finalize(*init_state));
            } else if (!agg.func_def) {
                // Complex aggregate (e.g. size(collect(...))): substitute internal
                // aggregate results with their empty-init values, then evaluate.
                std::unordered_map<const function::FunctionDef*, Value> subs;
                for (size_t j = 0; j < aggregates_.size(); ++j) {
                    if (aggregates_[j].is_internal && aggregates_[j].func_def && aggregates_[j].func_def->agg_init &&
                        aggregates_[j].func_def->agg_finalize) {
                        auto init_state = aggregates_[j].func_def->agg_init();
                        subs[aggregates_[j].func_def] = aggregates_[j].func_def->agg_finalize(*init_state);
                    }
                }
                DataChunk eval_chunk;
                eval_chunk.count = 1;
                ExpressionEvaluator out_eval(eval_ctx_);
                out_eval.aggregate_substitutions = &subs;
                auto result_col = Column::flat(binder::BoundTypeKind::ANY, 1);
                out_eval.evaluate(agg.arguments[0], eval_chunk, result_col);
                output.columns[out_idx].setValue(0, result_col.getValue(0));
            } else {
                output.columns[out_idx].setNull(0);
            }
            ++out_idx;
        }
        output.count = 1;
    }

    if (output.count > 0) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
