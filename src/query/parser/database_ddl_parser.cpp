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

/// Strip one layer of backticks from a label/relationship-type name.
///
/// Labels may legitimately contain spaces, so the quoted form is written with
/// backticks around the whole name. The whitespace tokenizer splits that into
/// several tokens, so the name is re-joined from every token past the keyword
/// and the surrounding backticks (if the whole name was quoted) are removed.
/// The schema stores the bare name -- quoting is syntax, not part of the name.
std::string joinQuotedName(const std::vector<std::string>& tokens, size_t from) {
    std::string name;
    for (size_t i = from; i < tokens.size(); ++i) {
        if (!name.empty())
            name += ' ';
        name += tokens[i];
    }
    if (name.size() >= 2 && name.front() == '`' && name.back() == '`')
        name = name.substr(1, name.size() - 2);
    return name;
}

/// Build a `DESCRIBE LABEL <name>` / `DESCRIBE RELATIONSHIP <name>` statement.
DatabaseDdlStatement makeDescribe(DatabaseDdlStatement::Type type, const std::vector<std::string>& tokens,
                                  size_t name_from) {
    DatabaseDdlStatement stmt;
    stmt.type = type;
    stmt.name = joinQuotedName(tokens, name_from);
    return stmt;
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

    // DESCRIBE family -- schema introspection (see the header for why these are
    // graph-scoped rather than SHOW-style). `DESC` is accepted as an alias.
    //
    // Order matters: the plural forms must be checked before the singular ones,
    // and the singular forms require a name, so `DESCRIBE LABEL` alone stays
    // unmatched and falls through to the Cypher parser (which reports a syntax
    // error) instead of being read as a label literally named "LABEL".
    //
    // The plural/singular pair is deliberately symmetric -- LABELS/LABEL and
    // RELATIONSHIPS/RELATIONSHIP -- so one rule covers both: plural lists every
    // schema entry, singular reports the fields of the one named.
    if (first == "DESCRIBE" || first == "DESC") {
        if (tokens.size() >= 2) {
            const std::string target = toUpper(tokens[1]);
            if (target == "LABELS") {
                DatabaseDdlStatement stmt;
                stmt.type = DatabaseDdlStatement::DESCRIBE_LABELS;
                return stmt;
            }
            if (target == "RELATIONSHIPS") {
                DatabaseDdlStatement stmt;
                stmt.type = DatabaseDdlStatement::DESCRIBE_RELATIONSHIPS;
                return stmt;
            }
            if (target == "RELATIONSHIP") {
                if (tokens.size() >= 3)
                    return makeDescribe(DatabaseDdlStatement::DESCRIBE_RELATIONSHIP, tokens, 2);
                return std::nullopt;
            }
            if (target == "REL") {
                if (tokens.size() >= 3)
                    return makeDescribe(DatabaseDdlStatement::DESCRIBE_RELATIONSHIP, tokens, 2);
                return std::nullopt;
            }
            if (target == "LABEL" && tokens.size() >= 3)
                return makeDescribe(DatabaseDdlStatement::DESCRIBE_LABEL, tokens, 2);
        }
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace eugraph
