#pragma once

#include "gen-cpp2/eugraph_types.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace eugraph::thrift_fmt {

// Format a double per Cypher TCK expectations:
// - Round-trip validation: prefer digits10 if it round-trips, else use max_digits10
// - Fixed decimal for moderate values (|v| in ~[1e-5, 1e16])
// - Scientific notation for extremes (no '+' sign, no leading zeros in exponent)
// - Negative zero -> "0.0"
inline std::string formatDouble(double d) {
    if (std::isnan(d))
        return "NaN";
    if (std::isinf(d))
        return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0.0 && std::signbit(d))
        return "0.0";

    auto fmt_with_prec = [](double d, int prec) {
        std::ostringstream oss;
        oss << std::setprecision(prec) << d;
        return oss.str();
    };

    auto fix_exp = [](std::string& s) {
        auto epos = s.find('e');
        if (epos == std::string::npos)
            epos = s.find('E');
        if (epos != std::string::npos && epos + 1 < s.size() && s[epos + 1] == '+') {
            s.erase(epos + 1, 1);
            epos = s.find('e');
            if (epos == std::string::npos)
                epos = s.find('E');
        }
        if (epos != std::string::npos && epos + 2 < s.size() && s[epos + 1] == '-') {
            size_t nz = epos + 2;
            while (nz < s.size() && s[nz] == '0')
                ++nz;
            if (nz > epos + 2)
                s.erase(epos + 2, nz - (epos + 2));
        }
    };

    // Find the shortest fixed-decimal representation that round-trips.
    // Try progressively fewer fractional digits to avoid noise like
    // "0.0000099999999999999" when "0.00001" suffices.
    auto try_fixed = [&](double d) -> std::string {
        for (int prec = 0; prec <= std::numeric_limits<double>::max_digits10; ++prec) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(prec) << d;
            std::string f = oss.str();
            auto dot = f.find('.');
            if (dot != std::string::npos) {
                size_t end = f.size() - 1;
                while (end > dot + 1 && f[end] == '0')
                    --end;
                f.erase(end + 1);
            }
            if (std::stod(f) == d)
                return f;
        }
        // Fallback: max precision (should never fail to round-trip)
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(std::numeric_limits<double>::max_digits10) << d;
        return oss.str();
    };

    // Try digits10 first (shorter), then max_digits10. Pick the shorter
    // representation that round-trips to the correct double value.
    std::string s_d10 = fmt_with_prec(d, std::numeric_limits<double>::digits10);
    std::string s_m10 = fmt_with_prec(d, std::numeric_limits<double>::max_digits10);
    fix_exp(s_d10);
    fix_exp(s_m10);

    bool d10_ok = (std::stod(s_d10) == d);
    bool m10_ok = (std::stod(s_m10) == d);

    std::string s;
    if (d10_ok)
        s = s_d10; // shorter is valid, use it
    else if (m10_ok)
        s = s_m10;
    else
        s = s_m10; // fallback

    // If defaultfloat produced scientific notation, try fixed as well.
    // Prefer fixed decimal unless it is excessively long (>20 chars),
    // matching Cypher TCK expectation for human-readable ranges.
    if (s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
        std::string fixed = try_fixed(d);
        if (fixed.size() <= 20 && std::stod(fixed) == d)
            return fixed;
    }

    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos)
        s += ".0";
    return s;
}

std::string formatResultValue(const thrift::ResultValue& rv);
std::string propertyTypeToString(thrift::PropertyType t);
thrift::PropertyType parsePropertyType(const std::string& type_str);

} // namespace eugraph::thrift_fmt
