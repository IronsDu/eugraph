#include "query/planner/binder/bind_binary_op.hpp"

#include "query/evaluator/columnar_kernels.hpp"

namespace eugraph {
namespace binder {
namespace {
bool isTemporalType(BoundTypeKind k) {
    return k == BoundTypeKind::DATETIME || k == BoundTypeKind::TIME || k == BoundTypeKind::DURATION;
}
} // namespace

using namespace eugraph::compute::detail;

binder::BinaryFallbackFn resolveBinaryFallbackFn(cypher::BinaryOperator op, binder::BoundTypeKind left_type,
                                                 binder::BoundTypeKind right_type) {
    using BO = cypher::BinaryOperator;
    using BTK = binder::BoundTypeKind;

    // EQ / NEQ — generic, works for all types via Value comparison
    if (op == BO::EQ)
        return genericEqBatch;
    if (op == BO::NEQ)
        return genericNeqBatch;

    // AND / OR / XOR — bool extraction
    if (op == BO::AND)
        return boolAndBatch;
    if (op == BO::OR)
        return boolOrBatch;
    if (op == BO::XOR)
        return boolXorBatch;

    // IN — scalar IN list -> bool
    if (op == BO::IN)
        return inBatch;

    // STARTS_WITH / ENDS_WITH / CONTAINS: accept any types.
    // Non-string operands return null at runtime via the string batch functions.
    if (op == BO::STARTS_WITH)
        return stringStartsWithBatch;
    if (op == BO::ENDS_WITH)
        return stringEndsWithBatch;
    if (op == BO::CONTAINS)
        return stringContainsBatch;

    // Bool ordered comparison (true > false).
    if (left_type == BTK::BOOL && right_type == BTK::BOOL) {
        switch (op) {
        case BO::LT:
            return boolLtBatch;
        case BO::GT:
            return boolGtBatch;
        case BO::LTE:
            return boolLteBatch;
        case BO::GTE:
            return boolGteBatch;
        default:
            return nullptr;
        }
    }

    // String-specific operations (accept NULL for null operands)
    if ((left_type == BTK::STRING || left_type == BTK::NULL_TYPE) &&
        (right_type == BTK::STRING || right_type == BTK::NULL_TYPE)) {
        switch (op) {
        case BO::ADD:
            return stringConcatBatch;
        case BO::LT:
            return stringLtBatch;
        case BO::GT:
            return stringGtBatch;
        case BO::LTE:
            return stringLteBatch;
        case BO::GTE:
            return stringGteBatch;
        default:
            return nullptr;
        }
    }

    // Int64-specific operations
    if (left_type == BTK::INT64 && right_type == BTK::INT64) {
        switch (op) {
        case BO::ADD:
            return int64AddBatch;
        case BO::SUB:
            return int64SubBatch;
        case BO::MUL:
            return int64MulBatch;
        case BO::DIV:
            return int64DivBatch;
        case BO::MOD:
            return int64ModBatch;
        case BO::POW:
            return int64PowBatch;
        case BO::LT:
            return int64LtBatch;
        case BO::GT:
            return int64GtBatch;
        case BO::LTE:
            return int64LteBatch;
        case BO::GTE:
            return int64GteBatch;
        default:
            return nullptr;
        }
    }

    // Double-specific operations
    if (left_type == BTK::DOUBLE && right_type == BTK::DOUBLE) {
        switch (op) {
        case BO::ADD:
            return doubleAddBatch;
        case BO::SUB:
            return doubleSubBatch;
        case BO::MUL:
            return doubleMulBatch;
        case BO::DIV:
            return doubleDivBatch;
        case BO::MOD:
            return doubleModBatch;
        case BO::POW:
            return doublePowBatch;
        case BO::LT:
            return doubleLtBatch;
        case BO::GT:
            return doubleGtBatch;
        case BO::LTE:
            return doubleLteBatch;
        case BO::GTE:
            return doubleGteBatch;
        default:
            return nullptr;
        }
    }

    // INT64+DOUBLE cross-type: promote int64→double at runtime.
    if ((left_type == BTK::INT64 && right_type == BTK::DOUBLE) ||
        (left_type == BTK::DOUBLE && right_type == BTK::INT64)) {
        switch (op) {
        case BO::ADD:
            return genericAddBatch;
        case BO::SUB:
            return genericSubBatch;
        case BO::MUL:
            return genericMulBatch;
        case BO::DIV:
            return genericDivBatch;
        case BO::MOD:
            return genericModBatch;
        case BO::POW:
            return genericPowBatch;
        case BO::LT:
            return genericLtBatch;
        case BO::GT:
            return genericGtBatch;
        case BO::LTE:
            return genericLteBatch;
        case BO::GTE:
            return genericGteBatch;
        default:
            return nullptr;
        }
    }

    // Temporal * number / Temporal / number (must precede ANY fallback
    // so that ANY-typed temporal properties dispatch correctly).
    // Only intercept MUL/DIV; other ops fall through to generic dispatch.
    if ((isTemporalType(left_type) || left_type == BTK::ANY) &&
        (right_type == BTK::INT64 || right_type == BTK::DOUBLE)) {
        switch (op) {
        case BO::MUL:
            return temporalMulBatch;
        case BO::DIV:
            return temporalDivBatch;
        default:
            break;
        }
    }

    // number * Temporal (commutative MUL, accept ANY as temporal)
    if ((left_type == BTK::INT64 || left_type == BTK::DOUBLE) &&
        (isTemporalType(right_type) || right_type == BTK::ANY)) {
        switch (op) {
        case BO::MUL:
            return temporalMulBatch;
        default:
            break;
        }
    }

    // ANY-type fallback: properties stored as ANY need runtime dispatch.
    // ANY + concrete → use concrete type's batch function.
    // ANY + ANY → use generic dispatch that inspects runtime Value types.
    if (left_type == BTK::ANY || right_type == BTK::ANY) {
        auto concrete = (left_type != BTK::ANY) ? left_type : (right_type != BTK::ANY) ? right_type : BTK::ANY;
        switch (concrete) {
        case BTK::INT64:
            switch (op) {
            case BO::ADD:
                return genericAddBatch;
            case BO::SUB:
                return genericSubBatch;
            case BO::MUL:
                return genericMulBatch;
            case BO::DIV:
                return genericDivBatch;
            case BO::MOD:
                return genericModBatch;
            case BO::POW:
                return genericPowBatch;
            case BO::LT:
                return genericLtBatch;
            case BO::GT:
                return genericGtBatch;
            case BO::LTE:
                return genericLteBatch;
            case BO::GTE:
                return genericGteBatch;
            default:
                return nullptr;
            }
        case BTK::DOUBLE:
            switch (op) {
            case BO::ADD:
                return genericAddBatch;
            case BO::SUB:
                return doubleSubBatch;
            case BO::MUL:
                return genericMulBatch;
            case BO::DIV:
                return genericDivBatch;
            case BO::MOD:
                return doubleModBatch;
            case BO::POW:
                return doublePowBatch;
            case BO::LT:
                return doubleLtBatch;
            case BO::GT:
                return doubleGtBatch;
            case BO::LTE:
                return doubleLteBatch;
            case BO::GTE:
                return doubleGteBatch;
            default:
                return nullptr;
            }
        case BTK::STRING:
            switch (op) {
            case BO::ADD:
                return genericAddBatch;
            case BO::LT:
                return stringLtBatch;
            case BO::GT:
                return stringGtBatch;
            case BO::LTE:
                return stringLteBatch;
            case BO::GTE:
                return stringGteBatch;
            default:
                return nullptr;
            }
        case BTK::BOOL:
            switch (op) {
            case BO::LT:
                return boolLtBatch;
            case BO::GT:
                return boolGtBatch;
            case BO::LTE:
                return boolLteBatch;
            case BO::GTE:
                return boolGteBatch;
            default:
                return nullptr;
            }
        case BTK::ANY: // both sides are ANY — runtime type dispatch
            switch (op) {
            case BO::ADD:
                return genericAddBatch;
            case BO::SUB:
                return genericSubBatch;
            case BO::MUL:
                return genericMulBatch;
            case BO::DIV:
                return genericDivBatch;
            case BO::MOD:
                return genericModBatch;
            case BO::POW:
                return genericPowBatch;
            case BO::LT:
                return genericLtBatch;
            case BO::GT:
                return genericGtBatch;
            case BO::LTE:
                return genericLteBatch;
            case BO::GTE:
                return genericGteBatch;
            default:
                return nullptr;
            }
        default:
            break;
        }
    }

    // Temporal: accept ANY as temporal (for property round-trip)
    bool left_temporal = (isTemporalType(left_type) || left_type == BTK::ANY);
    bool right_temporal = (isTemporalType(right_type) || right_type == BTK::ANY);

    // Temporal-specific: TEMPORAL + TEMPORAL (or ANY variants)
    if (left_temporal && right_temporal) {
        switch (op) {
        case BO::LT:
            return temporalLtBatch;
        case BO::GT:
            return temporalGtBatch;
        case BO::LTE:
            return temporalLteBatch;
        case BO::GTE:
            return temporalGteBatch;
        case BO::ADD:
            return temporalAddBatch;
        case BO::SUB:
            return temporalSubBatch;
        default:
            return nullptr;
        }
    }

    // List-specific operations (accept ANY for property round-trip)
    // Must be after temporal checks so temporal+ANY ADD is correctly dispatched
    if ((left_type == BTK::LIST || left_type == BTK::ANY) || (right_type == BTK::LIST || right_type == BTK::ANY)) {
        if (op == BO::ADD)
            return listConcatBatch;
        // Ordered comparison for list types (LT/GT/LTE/GTE)
        if ((left_type == BTK::LIST || left_type == BTK::ANY) && (right_type == BTK::LIST || right_type == BTK::ANY)) {
            switch (op) {
            case BO::LT:
                return listLtBatch;
            case BO::GT:
                return listGtBatch;
            case BO::LTE:
                return listLteBatch;
            case BO::GTE:
                return listGteBatch;
            default:
                break;
            }
        }
    }

    // Unhandled type combination: for ordered comparison, return null (incomparable types).
    if (op == BO::LT || op == BO::GT || op == BO::LTE || op == BO::GTE)
        return nullCmpBatch;

    return nullptr;
}

} // namespace binder
} // namespace eugraph
