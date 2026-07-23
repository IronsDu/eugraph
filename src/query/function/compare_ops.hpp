#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

/// Return a sort-order category for a Value, following Cypher type ordering:
///   map < node/vertex < edge < path < list < temporal < bool < string < number < NULL
inline int cypherTypeCategory(const Value& v) {
    return std::visit(
        [](const auto& x) -> int {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return 10;
            if constexpr (std::is_same_v<T, MapValue>)
                return 0;
            if constexpr (std::is_same_v<T, ListValue>)
                return 4;
            if constexpr (std::is_same_v<T, DateTimeValue> || std::is_same_v<T, TimeValue> ||
                          std::is_same_v<T, DurationValue>)
                return 5;
            if constexpr (std::is_same_v<T, bool>)
                return 6;
            if constexpr (std::is_same_v<T, std::string>)
                return 7;
            if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, double>)
                return 8;
            if constexpr (std::is_same_v<T, VertexRef> || std::is_same_v<T, VertexValue>)
                return 1;
            if constexpr (std::is_same_v<T, EdgeKey> || std::is_same_v<T, EdgeValue>)
                return 2;
            return 3; // PathTopology / PathValue
        },
        v);
}

// Forward-declare for recursive list comparison.
inline int cypherCompareValues(const Value& a, const Value& b);

// Helpers
inline int compareAsInt(int64_t la, int64_t lb) {
    if (la == lb)
        return 0;
    return la < lb ? -1 : 1;
}

inline int compareAsDouble(double a, double b) {
    if (a == b)
        return 0;
    if (std::isnan(a))
        return std::isnan(b) ? 0 : 1;
    if (std::isnan(b))
        return -1;
    return a < b ? -1 : 1;
}

inline int compareString(const std::string& a, const std::string& b) {
    if (a == b)
        return 0;
    return a < b ? -1 : 1;
}

inline int compareLists(const ListValue& la, const ListValue& lb) {
    size_t na = la.elements.size();
    size_t nb = lb.elements.size();
    for (size_t i = 0; i < na && i < nb; ++i) {
        int c = cypherCompareValues(la.elements[i].value, lb.elements[i].value);
        if (c != 0)
            return c;
    }
    if (na == nb)
        return 0;
    return na < nb ? -1 : 1;
}

template <typename T> inline int compareTemporal(const T& a, const T& b) {
    if (a.kind != b.kind)
        return 0;
    if (temporalLess(a, b))
        return -1;
    if (temporalLess(b, a))
        return 1;
    return 0;
}

inline int compareDuration(const DurationValue& a, const DurationValue& b) {
    int64_t totalA = a.months * 30LL * 86400LL + a.days * 86400LL + a.seconds * 1000000000LL + a.nanos;
    int64_t totalB = b.months * 30LL * 86400LL + b.days * 86400LL + b.seconds * 1000000000LL + b.nanos;
    if (totalA == totalB)
        return 0;
    return totalA < totalB ? -1 : 1;
}

/// Compare two Values according to Cypher type ordering.
/// Returns -1 (a < b), 0 (a == b), or 1 (a > b).
inline int cypherCompareValues(const Value& a, const Value& b) {
    bool aNull = std::holds_alternative<std::monostate>(a);
    bool bNull = std::holds_alternative<std::monostate>(b);
    if (aNull || bNull) {
        if (aNull && bNull)
            return 0;
        return aNull ? 1 : -1; // NULL > non-NULL
    }

    int catA = cypherTypeCategory(a);
    int catB = cypherTypeCategory(b);
    if (catA != catB)
        return catA < catB ? -1 : 1;

    return std::visit(
        [&b](const auto& la) -> int {
            using A = std::decay_t<decltype(la)>;
            if constexpr (std::is_same_v<A, int64_t>) {
                if (auto* db = std::get_if<double>(&b))
                    return compareAsDouble(static_cast<double>(la), *db);
                return compareAsInt(la, std::get<int64_t>(b));
            }
            if constexpr (std::is_same_v<A, double>) {
                if (auto* ib = std::get_if<int64_t>(&b))
                    return compareAsDouble(la, static_cast<double>(*ib));
                return compareAsDouble(la, std::get<double>(b));
            }
            if constexpr (std::is_same_v<A, std::string>)
                return compareString(la, std::get<std::string>(b));
            if constexpr (std::is_same_v<A, bool>) {
                bool lb = std::get<bool>(b);
                return la == lb ? 0 : (la ? 1 : -1);
            }
            if constexpr (std::is_same_v<A, ListValue>)
                return compareLists(la, std::get<ListValue>(b));
            if constexpr (std::is_same_v<A, DateTimeValue>)
                return compareTemporal(la, std::get<DateTimeValue>(b));
            if constexpr (std::is_same_v<A, TimeValue>)
                return compareTemporal(la, std::get<TimeValue>(b));
            if constexpr (std::is_same_v<A, DurationValue>)
                return compareDuration(la, std::get<DurationValue>(b));
            return 0;
        },
        a);
}

} // namespace compute
} // namespace eugraph
