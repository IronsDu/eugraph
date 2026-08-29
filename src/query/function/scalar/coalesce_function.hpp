#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/function/function_def.hpp"
#include "query/function/scalar/support/typed_batch.hpp"

namespace eugraph {
namespace function {
namespace scalar {

// --- coalesce ---

inline void coalesceBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (!args.empty()) {
        const auto kind = args[0]->type;
        bool same = true;
        for (const auto* col : args) {
            if (col->type != kind) {
                same = false;
                break;
            }
        }
        if (same && kind == binder::BoundTypeKind::INT64 && result.type == binder::BoundTypeKind::INT64) {
            typedCoalesceBatch<int64_t>(args, result, count);
            return;
        }
        if (same && kind == binder::BoundTypeKind::DOUBLE && result.type == binder::BoundTypeKind::DOUBLE) {
            typedCoalesceBatch<double>(args, result, count);
            return;
        }
        if (same && kind == binder::BoundTypeKind::STRING && result.type == binder::BoundTypeKind::STRING) {
            typedCoalesceBatch<std::string>(args, result, count);
            return;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        Value found{};
        for (auto* col : args) {
            auto v = col->getValue(i);
            if (!isNull(v)) {
                found = std::move(v);
                break;
            }
        }
        result.setValue(i, std::move(found));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
