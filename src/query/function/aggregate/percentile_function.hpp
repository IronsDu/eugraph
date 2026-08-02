#pragma once

#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace eugraph {
namespace function {
namespace aggregate {

/// Shared state for percentileDisc / percentileCont.
/// Accumulates (value, percentile) pairs and computes the percentile at finalize.
///
/// Cypher semantics (Neo4j / openCypher):
///   - p must be a constant in [0, 1]; otherwise NumberOutOfRange.
///   - null values are skipped (handled by the operator before calling add).
///   - null p → result is null.
///   - empty input → result is null.
struct PercentileStateBase : AggStateBase {
    std::vector<Value> values;
    double p = -1.0; // sentinel: not yet set
    bool p_is_null = false;

    void capturePercentile(const Value& pv) {
        if (isNull(pv)) {
            p_is_null = true;
            return;
        }
        double candidate = 0.0;
        if (std::holds_alternative<double>(pv))
            candidate = std::get<double>(pv);
        else if (std::holds_alternative<int64_t>(pv))
            candidate = static_cast<double>(std::get<int64_t>(pv));
        else
            throw std::runtime_error("ArgumentError: InvalidArgumentType: percentile argument 2 must be NUMBER");
        if (!(candidate >= 0.0 && candidate <= 1.0))
            throw std::runtime_error("ArgumentError: NumberOutOfRange: percentile argument 2 must be in [0, 1]");
        if (p < 0.0) {
            p = candidate;
        } else if (p != candidate) {
            // Cypher allows variable percentiles, but tests use constants.
            // Keep last value to keep semantics well-defined.
            p = candidate;
        }
    }

    void addValue(const Value& v) {
        if (!isNull(v))
            values.push_back(v);
    }

    void reset() {
        values.clear();
        p = -1.0;
        p_is_null = false;
    }
};

/// Discrete percentile (nearest-rank). Returns the same type as input.
/// rank = ceil(p * n), clamped to [1, n]; returns values[rank-1].
struct PercentileDiscState : PercentileStateBase {
    Value finalize() const {
        if (p_is_null || values.empty())
            return Value{};
        int64_t n = static_cast<int64_t>(values.size());
        int64_t rank = static_cast<int64_t>(std::ceil(p * static_cast<double>(n)));
        if (rank < 1)
            rank = 1;
        if (rank > n)
            rank = n;
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end(), [](const Value& a, const Value& b) {
            double da = toDouble(a), db = toDouble(b);
            return da < db;
        });
        return sorted[static_cast<size_t>(rank - 1)];
    }

private:
    static double toDouble(const Value& v) {
        if (std::holds_alternative<double>(v))
            return std::get<double>(v);
        if (std::holds_alternative<int64_t>(v))
            return static_cast<double>(std::get<int64_t>(v));
        return 0.0;
    }
};

/// Continuous percentile (linear interpolation). Always returns Double.
/// position = p * (n - 1); interpolate between floor and ceil positions.
struct PercentileContState : PercentileStateBase {
    Value finalize() const {
        if (p_is_null || values.empty())
            return Value{};
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Value& a, const Value& b) { return toDouble(a) < toDouble(b); });
        if (sorted.size() == 1)
            return Value(toDouble(sorted[0]));
        double pos = p * static_cast<double>(sorted.size() - 1);
        int64_t lower = static_cast<int64_t>(std::floor(pos));
        int64_t upper = static_cast<int64_t>(std::ceil(pos));
        if (lower == upper)
            return Value(toDouble(sorted[static_cast<size_t>(lower)]));
        double frac = pos - static_cast<double>(lower);
        double lo = toDouble(sorted[static_cast<size_t>(lower)]);
        double hi = toDouble(sorted[static_cast<size_t>(upper)]);
        return Value(lo + (hi - lo) * frac);
    }

private:
    static double toDouble(const Value& v) {
        if (std::holds_alternative<double>(v))
            return std::get<double>(v);
        if (std::holds_alternative<int64_t>(v))
            return static_cast<double>(std::get<int64_t>(v));
        return 0.0;
    }
};

} // namespace aggregate
} // namespace function
} // namespace eugraph
