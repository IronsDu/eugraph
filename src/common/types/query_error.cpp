#include "common/types/query_error.hpp"

#include <array>

namespace eugraph {
namespace {

/// 下标即 QueryErrorKind 的取值，顺序必须与枚举一致。
constexpr std::array<const char*, 5> kTokens = {"SyntaxError", "TypeError", "ArgumentError", "ArithmeticError",
                                                "ExecutionFailed"};

constexpr std::array<const char*, 5> kStatusCodes = {
    "Neo.ClientError.Statement.SyntaxError",       "Neo.ClientError.Statement.TypeError",
    "Neo.ClientError.Statement.ArgumentError",     "Neo.ClientError.Statement.ArithmeticError",
    "Neo.DatabaseError.Statement.ExecutionFailed",
};

static_assert(kTokens.size() == static_cast<size_t>(QueryErrorKind::ExecutionFailed) + 1,
              "kTokens 必须与 QueryErrorKind 一一对应");
static_assert(kStatusCodes.size() == kTokens.size(), "状态码表必须与分类表等长");

} // namespace

const char* queryErrorToken(QueryErrorKind kind) noexcept {
    return kTokens[static_cast<size_t>(kind)];
}

const char* neo4jStatusCode(QueryErrorKind kind) noexcept {
    return kStatusCodes[static_cast<size_t>(kind)];
}

QueryErrorKind classifyQueryErrorMessage(std::string_view message) noexcept {
    // 取最先出现的 token：binder 会把多个错误用 "; " 拼接，第一个才是根因
    // （"Binding failed; SyntaxError: ..."）。同位置时按枚举顺序取更具体的一类。
    size_t best_pos = std::string_view::npos;
    QueryErrorKind best_kind = QueryErrorKind::ExecutionFailed;
    for (size_t i = 0; i < kTokens.size(); ++i) {
        const size_t pos = message.find(kTokens[i]);
        if (pos == std::string_view::npos)
            continue;
        const auto kind = static_cast<QueryErrorKind>(i);
        // ExecutionFailed 是兜底分类：只有在没有更具体 token 时才采用它。
        if (kind == QueryErrorKind::ExecutionFailed)
            continue;
        if (pos < best_pos) {
            best_pos = pos;
            best_kind = kind;
        }
    }
    return best_kind;
}

QueryException::QueryException(QueryErrorKind kind, std::string message)
    : std::runtime_error(std::string(queryErrorToken(kind)) + ": " + message), kind_(kind),
      message_(std::move(message)) {}

} // namespace eugraph
