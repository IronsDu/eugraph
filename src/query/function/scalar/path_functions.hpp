#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"

#include <string>

namespace eugraph {
namespace function {
namespace scalar {

/// nodes(path) -> list of vertices (even-indexed elements)
inline Value nodesImpl(const Value& arg) {
    if (std::holds_alternative<PathValue>(arg)) {
        const auto& pv = std::get<PathValue>(arg);
        ListValue lv;
        for (size_t i = 0; i < pv.elements.size(); i += 2)
            lv.elements.push_back(pv.elements[i]);
        return Value(std::move(lv));
    }
    if (std::holds_alternative<PathTopology>(arg)) {
        const auto& pt = std::get<PathTopology>(arg);
        ListValue lv;
        for (size_t i = 0; i < pt.vertex_ids.size(); ++i) {
            VertexValue vv;
            vv.id = pt.vertex_ids[i];
            lv.elements.push_back(ValueStorage{Value(std::move(vv))});
        }
        return Value(std::move(lv));
    }
    return Value{};
}

/// relationships(path) -> list of edges (odd-indexed elements)
inline Value relationshipsImpl(const Value& arg) {
    if (std::holds_alternative<PathValue>(arg)) {
        const auto& pv = std::get<PathValue>(arg);
        ListValue lv;
        for (size_t i = 1; i < pv.elements.size(); i += 2)
            lv.elements.push_back(pv.elements[i]);
        return Value(std::move(lv));
    }
    if (std::holds_alternative<PathTopology>(arg)) {
        const auto& pt = std::get<PathTopology>(arg);
        ListValue lv;
        for (size_t i = 0; i < pt.edge_ids.size(); ++i) {
            EdgeValue ev;
            ev.id = pt.edge_ids[i];
            ev.label_id = pt.edge_label_ids[i];
            ev.seq = pt.seqs[i];
            lv.elements.push_back(ValueStorage{Value(std::move(ev))});
        }
        return Value(std::move(lv));
    }
    return Value{};
}

/// length(path) -> number of edges in path
inline Value lengthImpl(const Value& arg) {
    if (std::holds_alternative<PathValue>(arg))
        return Value(static_cast<int64_t>((std::get<PathValue>(arg).elements.size() - 1) / 2));
    if (std::holds_alternative<PathTopology>(arg))
        return Value(static_cast<int64_t>(std::get<PathTopology>(arg).edge_ids.size()));
    return Value{};
}

/// Unified scalar callbacks for FunctionRegistry.
inline Value nodesScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return nodesImpl(args[0]);
}

inline Value relationshipsScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return relationshipsImpl(args[0]);
}

inline Value lengthScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return lengthImpl(args[0]);
}

/// Batch scalar callbacks.

inline void nodesBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                         const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, nodesImpl(arg_col.getValue(i)));
    }
}

inline void relationshipsBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                 const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, relationshipsImpl(arg_col.getValue(i)));
    }
}

inline void lengthBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                          const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, lengthImpl(arg_col.getValue(i)));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
