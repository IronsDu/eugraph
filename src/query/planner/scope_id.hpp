#pragma once

#include <cstdint>

namespace eugraph {
namespace binder {

/// Monotonic identifier for a binding scope. Scope is a visibility /
/// provenance concept; the semantic identity of a variable is its SlotId.
using ScopeId = uint32_t;
constexpr ScopeId kRootScope = 0;
constexpr ScopeId INVALID_SCOPE_ID = UINT32_MAX;

} // namespace binder
} // namespace eugraph
