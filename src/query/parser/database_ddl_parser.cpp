#include "query/parser/database_ddl_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace eugraph {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string toUpper(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::toupper);
    return r;
}

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok)
        tokens.push_back(tok);
    return tokens;
}

} // namespace

std::optional<DatabaseDdlStatement> DatabaseDdlParser::tryParse(const std::string& query) {
    std::string trimmed = trim(query);
    if (trimmed.empty())
        return std::nullopt;

    auto tokens = tokenize(trimmed);
    if (tokens.empty())
        return std::nullopt;

    std::string first = toUpper(tokens[0]);

    // CREATE DATABASE <name>
    if (first == "CREATE" && tokens.size() >= 3 && toUpper(tokens[1]) == "DATABASE") {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::CREATE_DATABASE;
        stmt.name = tokens[2];
        return stmt;
    }

    // DROP DATABASE <name>
    if (first == "DROP" && tokens.size() >= 3 && toUpper(tokens[1]) == "DATABASE") {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::DROP_DATABASE;
        stmt.name = tokens[2];
        return stmt;
    }

    // SHOW DATABASES
    if (first == "SHOW" && tokens.size() >= 2 && toUpper(tokens[1]) == "DATABASES") {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::SHOW_DATABASES;
        stmt.yield_all = std::any_of(tokens.begin(), tokens.end(), [](const auto& t) { return toUpper(t) == "YIELD"; });
        return stmt;
    }

    // SHOW DATABASE <name>
    if (first == "SHOW" && tokens.size() >= 3 && toUpper(tokens[1]) == "DATABASE") {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::SHOW_DATABASE;
        stmt.name = tokens[2];
        return stmt;
    }

    // SHOW PROCEDURES [YIELD ...]
    if (first == "SHOW" && tokens.size() >= 2 && toUpper(tokens[1]) == "PROCEDURES") {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::SHOW_PROCEDURES;
        return stmt;
    }

    // SHOW FUNCTIONS [YIELD ...]
    if (first == "SHOW" && tokens.size() >= 2 && toUpper(tokens[1]) == "FUNCTIONS") {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::SHOW_FUNCTIONS;
        return stmt;
    }

    // SHOW CURRENT USER
    if (first == "SHOW" && tokens.size() >= 3 && toUpper(tokens[1]) == "CURRENT" && toUpper(tokens[2]) == "USER") {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::SHOW_CURRENT_USER;
        return stmt;
    }

    // SHOW VECTOR INDEXES [YIELD ...]
    if (first == "SHOW" && tokens.size() >= 3 && toUpper(tokens[1]) == "VECTOR" && toUpper(tokens[2]) == "INDEXES") {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::SHOW_VECTOR_INDEXES;
        return stmt;
    }

    // USE <name>
    if (first == "USE" && tokens.size() >= 2) {
        DatabaseDdlStatement stmt;
        stmt.type = DatabaseDdlStatement::USE_GRAPH;
        stmt.name = tokens[1];
        return stmt;
    }

    return std::nullopt;
}

} // namespace eugraph
