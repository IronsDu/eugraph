#include "query/planner/binder.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_call_op.hpp"

namespace eugraph {
namespace binder {

std::optional<BoundLogicalOperator> Binder::bindCall(const cypher::CallClause& call,
                                                     std::optional<BoundLogicalOperator> current) {
    // Only support known built-in procedures for now.
    if (call.procedure_name != "db.ping" && call.procedure_name != "db.ping()" &&
        call.procedure_name != "db.schema.visualization" && call.procedure_name != "dbms.clientConfig" &&
        call.procedure_name != "dbms.clientConfig()" && call.procedure_name != "db.indexes" &&
        call.procedure_name != "db.indexes()" && call.procedure_name != "dbms.procedures" &&
        call.procedure_name != "dbms.procedures()" && call.procedure_name != "dbms.components" &&
        call.procedure_name != "dbms.components()" && call.procedure_name != "dbms.functions" &&
        call.procedure_name != "dbms.functions()" && call.procedure_name != "db.labels" &&
        call.procedure_name != "db.labels()" && call.procedure_name != "db.relationshipTypes" &&
        call.procedure_name != "db.relationshipTypes()" && call.procedure_name != "db.propertyKeys" &&
        call.procedure_name != "db.propertyKeys()") {
        error("Unknown procedure: " + call.procedure_name);
        return std::nullopt;
    }

    auto call_op = std::make_unique<BoundCallOp>();
    call_op->procedure_name = call.procedure_name;
    for (const auto& arg : call.args) {
        auto bound_arg = bindExpression(arg);
        if (!bound_arg)
            return std::nullopt;
        call_op->arguments.push_back(std::move(*bound_arg));
    }
    for (const auto& yi : call.yield_items)
        call_op->yield_items.push_back(yi.alias.value_or(""));

    if (call.where_pred) {
        error("WHERE on CALL is not yet supported");
        return std::nullopt;
    }

    // Map procedure name to output schema.
    std::string proc_base = call.procedure_name;
    // Normalize: strip trailing "()" if present (parser may include it in the name)
    if (proc_base.size() > 2 && proc_base.substr(proc_base.size() - 2) == "()")
        proc_base = proc_base.substr(0, proc_base.size() - 2);

    if (proc_base == "db.ping") {
        call_op->output_names = {"success"};
        call_op->output_types = {BoundType::Bool()};
    } else if (proc_base == "db.schema.visualization") {
        call_op->output_names = {"nodes", "relationships"};
        call_op->output_types = {BoundType::List(BoundType::Any()), BoundType::List(BoundType::Any())};
    } else if (proc_base == "dbms.clientConfig") {
        // Neo4j Browser calls this on connect and reads one (name, value)
        // row per client setting.
        call_op->output_names = {"name", "value"};
        call_op->output_types = {BoundType::String(), BoundType::Any()};
    } else if (proc_base == "db.indexes") {
        call_op->output_names = {"id",   "name",       "state",         "populationPercent", "uniqueness",
                                 "type", "entityType", "labelsOrTypes", "properties",        "owningConstraint"};
        call_op->output_types = {BoundType::Int64(),
                                 BoundType::String(),
                                 BoundType::String(),
                                 BoundType::Double(),
                                 BoundType::String(),
                                 BoundType::String(),
                                 BoundType::String(),
                                 BoundType::List(BoundType::String()),
                                 BoundType::List(BoundType::String()),
                                 BoundType::Null()};
    } else if (proc_base == "dbms.procedures") {
        call_op->output_names = {"name", "signature", "description", "mode", "roles"};
        call_op->output_types = {BoundType::String(), BoundType::String(), BoundType::String(), BoundType::String(),
                                 BoundType::List(BoundType::String())};
    } else if (proc_base == "dbms.components") {
        call_op->output_names = {"name", "versions", "edition"};
        call_op->output_types = {BoundType::String(), BoundType::List(BoundType::String()), BoundType::String()};
    } else if (proc_base == "dbms.functions") {
        call_op->output_names = {"name", "signature", "description"};
        call_op->output_types = {BoundType::String(), BoundType::String(), BoundType::String()};
    } else if (proc_base == "db.labels") {
        call_op->output_names = {"label"};
        call_op->output_types = {BoundType::String()};
    } else if (proc_base == "db.relationshipTypes") {
        call_op->output_names = {"relationshipType"};
        call_op->output_types = {BoundType::String()};
    } else if (proc_base == "db.propertyKeys") {
        call_op->output_names = {"propertyKey"};
        call_op->output_types = {BoundType::String()};
    } else {
        error("Unknown procedure: " + call.procedure_name);
        return std::nullopt;
    }

    // Register output columns in scope
    for (size_t i = 0; i < call_op->output_names.size(); ++i) {
        registerColumn(call_op->output_names[i], call_op->output_types[i]);
    }

    if (current.has_value()) {
        // CALL clause in the middle of a query: cross join with current context
        auto join = std::make_unique<BoundBinaryJoinOp>();
        join->join_type = JoinType::Cross;
        join->left = std::move(*current);
        join->right = std::move(call_op);
        return join;
    }

    return BoundLogicalOperator(std::move(call_op));
}

} // namespace binder
} // namespace eugraph
