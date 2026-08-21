#pragma once

#include <string>

namespace eugraph {
namespace binder {

/// Internal name prefixes for the left/right operands of cross-product
/// equality predicates. The variable name is appended after the prefix.
/// These are never user variables; DPL must not allocate slots for them by
/// name or promote them via ProjectionExtract, and ExpressionCompiler must
/// keep their physical column indices instead of resolving by slot.
inline constexpr const char* kJoinEqualityLeft = "__eq_left__";
inline constexpr const char* kJoinEqualityRight = "__eq_right__";

inline bool isJoinEqualityLeft(const std::string& name) {
    return name.rfind(kJoinEqualityLeft, 0) == 0;
}

inline bool isJoinEqualityRight(const std::string& name) {
    return name.rfind(kJoinEqualityRight, 0) == 0;
}

inline bool isJoinEqualityRef(const std::string& name) {
    return isJoinEqualityLeft(name) || isJoinEqualityRight(name);
}

inline std::string joinEqualityVarName(const std::string& ref_name, const char* prefix) {
    return ref_name.rfind(prefix, 0) == 0 ? ref_name.substr(std::char_traits<char>::length(prefix)) : ref_name;
}

} // namespace binder
} // namespace eugraph
