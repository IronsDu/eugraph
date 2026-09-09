#pragma once

namespace eugraph {
namespace binder {

enum class JoinType {
    Cross,
    Inner,
    Left,
    Hash
};

} // namespace binder
} // namespace eugraph
