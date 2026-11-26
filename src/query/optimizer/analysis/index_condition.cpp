#include "query/optimizer/analysis/index_condition.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace eugraph {
namespace optimizer {

int compareIndexValues(const PropertyValue& a, const PropertyValue& b) {
    if (auto* ai = std::get_if<int64_t>(&a)) {
        if (auto* bi = std::get_if<int64_t>(&b))
            return *ai < *bi ? -1 : (*ai > *bi ? 1 : 0);
    }
    if (auto* ad = std::get_if<double>(&a)) {
        if (auto* bd = std::get_if<double>(&b))
            return *ad < *bd ? -1 : (*ad > *bd ? 1 : 0);
    }
    if (auto* as = std::get_if<std::string>(&a)) {
        if (auto* bs = std::get_if<std::string>(&b))
            return *as < *bs ? -1 : (*as > *bs ? 1 : 0);
    }
    if (auto* ab = std::get_if<bool>(&a)) {
        if (auto* bb = std::get_if<bool>(&b))
            return *ab < *bb ? -1 : (*ab > *bb ? 1 : 0);
    }
    return 0;
}

void mergeIndexLower(IndexRange& range, const PropertyValue& value, bool inclusive) {
    if (!range.lower || compareIndexValues(value, range.lower->value) > 0) {
        range.lower = IndexBound{value, inclusive};
        return;
    }
    if (compareIndexValues(value, range.lower->value) == 0)
        range.lower->inclusive = range.lower->inclusive && inclusive;
}

void mergeIndexUpper(IndexRange& range, const PropertyValue& value, bool inclusive) {
    if (!range.upper || compareIndexValues(value, range.upper->value) < 0) {
        range.upper = IndexBound{value, inclusive};
        return;
    }
    if (compareIndexValues(value, range.upper->value) == 0)
        range.upper->inclusive = range.upper->inclusive && inclusive;
}

} // namespace optimizer
} // namespace eugraph
