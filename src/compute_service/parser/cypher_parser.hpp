#pragma once

#include "compute_service/parser/ast.hpp"
#include <memory>
#include <string>
#include <variant>

namespace eugraph {
namespace cypher {

struct ParseError {
    std::string message;
    int line = 0;
    int column = 0;
};

class CypherQueryParser {
public:
    CypherQueryParser();
    ~CypherQueryParser();

    // 解析 Cypher 查询字符串，成功返回 Statement，失败返回 ParseError
    // 线程安全：每次调用创建独立的 ANTLR lexer/parser 实例
    std::variant<Statement, ParseError> parse(const std::string& cypher_text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Backward-compatible alias
using CypherParser = CypherQueryParser;

} // namespace cypher
} // namespace eugraph
