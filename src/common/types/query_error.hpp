#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace eugraph {

/// Cypher 错误的分类，每一项对应一个 Neo4j 状态码（Bolt FAILURE 的 `code` 字段）。
///
/// 分类 token 沿用仓库既有约定（错误文本里的 `SyntaxError:` / `TypeError: ` 等前缀，
/// TCK 的分类器也依赖它），因此旧代码里 `throw std::runtime_error("TypeError: ...")`
/// 无需逐个改写：`classifyQueryErrorMessage()` 在服务层统一解释这些前缀。
enum class QueryErrorKind : uint8_t {
    Syntax,          ///< 解析/绑定错误：Neo.ClientError.Statement.SyntaxError
    Type,            ///< 类型错误：Neo.ClientError.Statement.TypeError
    Argument,        ///< 参数取值错误：Neo.ClientError.Statement.ArgumentError
    Arithmetic,      ///< 整数溢出/除零：Neo.ClientError.Statement.ArithmeticError
    ExecutionFailed, ///< 运行期执行失败：Neo.DatabaseError.Statement.ExecutionFailed
};

/// 分类 token（= 错误消息里使用的词，如 "TypeError"）。
const char* queryErrorToken(QueryErrorKind kind) noexcept;

/// Neo4j 标准状态码（Bolt 客户端据此判定错误类别）。
const char* neo4jStatusCode(QueryErrorKind kind) noexcept;

/// 从错误消息里识别分类。
///
/// 消息中的分类 token 是既有约定（binder 的 `error(...)` 与函数/算子里的
/// `throw std::runtime_error("...Error: ...")`）。识别不出时按 ExecutionFailed
/// 处理：能走到这里说明错误发生在执行期。
QueryErrorKind classifyQueryErrorMessage(std::string_view message) noexcept;

/// 带分类的查询错误。
///
/// `what()` 保留 token 前缀（Thrift 客户端与 TCK 依赖文本分类）；
/// `message()` 是不带前缀的原始信息，Bolt FAILURE 的 message 用它，与 Neo4j 对齐。
class QueryException : public std::runtime_error {
public:
    QueryException(QueryErrorKind kind, std::string message);

    QueryErrorKind kind() const noexcept {
        return kind_;
    }
    const std::string& message() const noexcept {
        return message_;
    }
    const char* code() const noexcept {
        return neo4jStatusCode(kind_);
    }

private:
    QueryErrorKind kind_;
    std::string message_;
};

} // namespace eugraph
