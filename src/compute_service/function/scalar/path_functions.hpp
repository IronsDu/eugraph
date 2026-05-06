#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

#include <string>

namespace eugraph {
namespace function {
namespace scalar {

/// nodes(path) -> list of vertices (even-indexed elements)
inline Value nodesImpl(const Value& arg) {
    if (!std::holds_alternative<PathValue>(arg))
        return Value{};
    const auto& pv = std::get<PathValue>(arg);
    ListValue lv;
    for (size_t i = 0; i < pv.elements.size(); i += 2) {
        lv.elements.push_back(pv.elements[i]);
    }
    return Value(std::move(lv));
}

/// relationships(path) -> list of edges (odd-indexed elements)
inline Value relationshipsImpl(const Value& arg) {
    if (!std::holds_alternative<PathValue>(arg))
        return Value{};
    const auto& pv = std::get<PathValue>(arg);
    ListValue lv;
    for (size_t i = 1; i < pv.elements.size(); i += 2) {
        lv.elements.push_back(pv.elements[i]);
    }
    return Value(std::move(lv));
}

/// length(path) -> number of edges in path
inline Value lengthImpl(const Value& arg) {
    if (!std::holds_alternative<PathValue>(arg))
        return Value{};
    const auto& pv = std::get<PathValue>(arg);
    return Value(static_cast<int64_t>((pv.elements.size() - 1) / 2));
}

} // namespace scalar
} // namespace function
} // namespace eugraph
