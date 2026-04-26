#include "compute_service/parser/index_ddl_parser.hpp"

#include <algorithm>
#include <sstream>

namespace eugraph {

static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool expectWord(std::istringstream& iss, const std::string& expected) {
    std::string word;
    if (!(iss >> word))
        return false;
    return toUpper(word) == toUpper(expected);
}

static std::optional<std::string> readQuoted(std::istringstream& iss) {
    std::string result;
    char c;
    // skip whitespace
    while (iss.peek() == ' ')
        iss.get();
    if (!(iss >> c) || c != '(')
        return std::nullopt;
    // Read until closing )
    while (iss.get(c)) {
        if (c == ')')
            break;
        if (c != ' ' && c != '\t')
            result += c;
    }
    // Extract part after the dot (format: var.prop)
    auto dot_pos = result.find('.');
    if (dot_pos != std::string::npos)
        result = result.substr(dot_pos + 1);
    return trim(result);
}

std::optional<IndexDdlStatement> IndexDdlParser::tryParse(const std::string& query) {
    auto trimmed = trim(query);
    if (trimmed.empty())
        return std::nullopt;

    std::istringstream iss(trimmed);
    std::string cmd;
    iss >> cmd;
    auto upper_cmd = toUpper(cmd);

    if (upper_cmd == "CREATE") {
        std::string next;
        iss >> next;
        auto upper_next = toUpper(next);

        bool unique = false;
        if (upper_next == "UNIQUE") {
            unique = true;
            iss >> next;
            upper_next = toUpper(next);
        }

        if (upper_next != "INDEX")
            return std::nullopt;

        IndexDdlStatement stmt;
        stmt.unique = unique;

        // Read index name
        if (!(iss >> stmt.index_name))
            return std::nullopt;

        // FOR
        if (!expectWord(iss, "FOR"))
            return std::nullopt;

        // Determine vertex or edge index by looking at the pattern
        // Vertex: FOR (n:Label) ON (n.prop)
        // Edge: FOR ()-[r:TYPE]-() ON (r.prop)
        char c;
        while (iss.get(c) && c == ' ')
            ;
        if (c != '(')
            return std::nullopt;

        char next_c = iss.peek();
        if (next_c == ')') {
            // Edge index: ()-[r:TYPE]-()
            iss.get(); // consume ')'

            // Read '-'
            while (iss.get(c) && c == ' ')
                ;
            if (c != '-')
                return std::nullopt;

            // Read [r:TYPE]
            while (iss.get(c) && c == ' ')
                ;
            if (c != '[')
                return std::nullopt;
            std::string var;
            while (iss.get(c) && c != ':')
                var += c;
            // Read label name until ]
            std::string label;
            while (iss.get(c) && c != ']')
                label += c;
            stmt.label_name = trim(label);
            stmt.type = IndexDdlStatement::CREATE_EDGE_INDEX;

            // Read '-()'
            while (iss.get(c) && c == ' ')
                ;
            if (c != '-')
                return std::nullopt;
            while (iss.get(c) && c == ' ')
                ;
            if (c != '(')
                return std::nullopt;
            while (iss.get(c) && c != ')')
                ;
        } else {
            // Vertex index: (n:Label)
            iss.unget();
            std::string var_label;
            while (iss.get(c) && c != ')')
                var_label += c;
            // Parse var:label
            auto colon_pos = var_label.find(':');
            if (colon_pos == std::string::npos)
                return std::nullopt;
            stmt.label_name = trim(var_label.substr(colon_pos + 1));
            stmt.type = IndexDdlStatement::CREATE_VERTEX_INDEX;
        }

        // ON
        if (!expectWord(iss, "ON"))
            return std::nullopt;

        // (var.prop)
        auto prop = readQuoted(iss);
        if (!prop)
            return std::nullopt;
        stmt.property_name = *prop;

        return stmt;
    }

    if (upper_cmd == "DROP") {
        if (!expectWord(iss, "INDEX"))
            return std::nullopt;
        IndexDdlStatement stmt;
        stmt.type = IndexDdlStatement::DROP_INDEX;
        if (!(iss >> stmt.index_name))
            return std::nullopt;
        return stmt;
    }

    if (upper_cmd == "SHOW") {
        std::string next;
        iss >> next;
        auto upper_next = toUpper(next);
        if (upper_next == "INDEXES" || upper_next == "INDEX") {
            IndexDdlStatement stmt;
            // Check if there's a name after "SHOW INDEX"
            std::string name;
            if (iss >> name) {
                stmt.type = IndexDdlStatement::SHOW_INDEX;
                stmt.index_name = name;
            } else {
                if (upper_next == "INDEXES")
                    stmt.type = IndexDdlStatement::SHOW_INDEXES;
                else
                    stmt.type = IndexDdlStatement::SHOW_INDEXES;
            }
            return stmt;
        }
    }

    return std::nullopt;
}

} // namespace eugraph
