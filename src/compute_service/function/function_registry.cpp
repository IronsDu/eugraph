#include "compute_service/function/function_registry.hpp"

#include <climits>

#include "compute_service/function/aggregate/avg_function.hpp"
#include "compute_service/function/aggregate/count_function.hpp"
#include "compute_service/function/aggregate/min_function.hpp"
#include "compute_service/function/aggregate/min_function.hpp" // also provides MaxState
#include "compute_service/function/aggregate/sum_function.hpp"
#include "compute_service/function/scalar/id_function.hpp"
#include "compute_service/function/scalar/path_functions.hpp"

namespace eugraph {
namespace function {

FunctionRegistry::FunctionRegistry() {
    registerBuiltins();
}

void FunctionRegistry::registerBuiltins() {
    registerScalarBuiltins();
    registerAggregateBuiltins();
}

void FunctionRegistry::registerScalarBuiltins() {
    using binder::BoundType;

    // id(Vertex) -> Int64
    functions_["id"].push_back(
        {"id", {BoundType::Vertex()}, BoundType::Int64(), false, false, scalar::idScalarFn, {}, {}, {}});
    // id(Edge) -> Int64
    functions_["id"].push_back(
        {"id", {BoundType::Edge()}, BoundType::Int64(), false, false, scalar::idScalarFn, {}, {}, {}});

    // nodes(Path) -> List<Vertex>
    functions_["nodes"].push_back({"nodes",
                                   {BoundType::Path()},
                                   BoundType::List(BoundType::Vertex()),
                                   false,
                                   false,
                                   scalar::nodesScalarFn,
                                   {},
                                   {},
                                   {}});

    // relationships(Path) -> List<Edge>
    functions_["relationships"].push_back({"relationships",
                                           {BoundType::Path()},
                                           BoundType::List(BoundType::Edge()),
                                           false,
                                           false,
                                           scalar::relationshipsScalarFn,
                                           {},
                                           {},
                                           {}});

    // length(Path) -> Int64
    functions_["length"].push_back(
        {"length", {BoundType::Path()}, BoundType::Int64(), false, false, scalar::lengthScalarFn, {}, {}, {}});
}

void FunctionRegistry::registerAggregateBuiltins() {
    using binder::BoundType;

    // count(*) -> Int64  (represented as count with no typed args)
    functions_["count"].push_back(
        {"count",
         {},
         BoundType::Int64(),
         true,
         true,
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::CountState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::CountState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::CountState&>(s).finalize(); }});

    // sum(Int64) -> Int64
    functions_["sum"].push_back(
        {"sum",
         {BoundType::Int64()},
         BoundType::Int64(),
         true,
         false,
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::Int64SumState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::Int64SumState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::Int64SumState&>(s).finalize(); }});
    // sum(Double) -> Double
    functions_["sum"].push_back(
        {"sum",
         {BoundType::Double()},
         BoundType::Double(),
         true,
         false,
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::DoubleSumState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::DoubleSumState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::DoubleSumState&>(s).finalize(); }});

    // avg(Int64) -> Double (avg always returns double)
    functions_["avg"].push_back(
        {"avg",
         {BoundType::Int64()},
         BoundType::Double(),
         true,
         false,
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::AvgState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::AvgState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::AvgState&>(s).finalize(); }});
    // avg(Double) -> Double
    functions_["avg"].push_back(
        {"avg",
         {BoundType::Double()},
         BoundType::Double(),
         true,
         false,
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::AvgState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::AvgState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::AvgState&>(s).finalize(); }});

    // min(Any) -> Any
    functions_["min"].push_back(
        {"min",
         {},
         BoundType::Any(),
         true,
         true,
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::MinState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::MinState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::MinState&>(s).finalize(); }});

    // max(Any) -> Any
    functions_["max"].push_back(
        {"max",
         {},
         BoundType::Any(),
         true,
         true,
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::MaxState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::MaxState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::MaxState&>(s).finalize(); }});
}

void FunctionRegistry::registerFunction(FunctionDef def) {
    functions_[def.name].push_back(std::move(def));
}

const FunctionDef* FunctionRegistry::lookup(const std::string& name,
                                            const std::vector<binder::BoundType>& arg_types) const {
    auto it = functions_.find(name);
    if (it == functions_.end())
        return nullptr;

    const FunctionDef* best = nullptr;
    int best_cost = INT32_MAX;
    for (const auto& overload : it->second) {
        int cost = overload.matchCost(arg_types);
        if (cost >= 0 && cost < best_cost) {
            best_cost = cost;
            best = &overload;
        }
    }
    return best;
}

const std::vector<FunctionDef>* FunctionRegistry::lookupAll(const std::string& name) const {
    auto it = functions_.find(name);
    if (it == functions_.end())
        return nullptr;
    return &it->second;
}

bool FunctionRegistry::exists(const std::string& name) const {
    return functions_.find(name) != functions_.end();
}

} // namespace function
} // namespace eugraph
