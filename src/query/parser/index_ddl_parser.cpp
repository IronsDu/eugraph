#include "query/parser/index_ddl_parser.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

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

// Parse one property item: n.prop (weak) or n::Label.prop (strong).
static bool parsePropertyAccessor(const std::string& raw, IndexPropertyAccessor& out) {
    auto last_dot = raw.rfind('.');
    if (last_dot == std::string::npos)
        return false;
    std::string prefix = trim(raw.substr(0, last_dot));
    std::string prop = trim(raw.substr(last_dot + 1));
    if (prop.empty())
        return false;

    auto strong_pos = prefix.find("::");
    if (strong_pos != std::string::npos) {
        std::string source = trim(prefix.substr(strong_pos + 2));
        if (source.empty())
            return false;
        out.is_strong = true;
        out.source_label = source;
    }
    out.property_name = prop;
    return true;
}

// Read comma-separated properties from (n.prop1, n.prop2, ...)
// Returns empty vector on parse failure.
static std::vector<IndexPropertyAccessor> readPropertyList(std::istringstream& iss) {
    std::vector<IndexPropertyAccessor> result;
    std::vector<std::string> raw_items;
    std::string token;
    char c;
    // skip whitespace
    while (iss.peek() == ' ')
        iss.get();
    if (!(iss >> c) || c != '(')
        return {};
    while (iss.get(c)) {
        if (c == ')')
            break;
        if (c == ',') {
            if (!token.empty()) {
                raw_items.push_back(trim(token));
                token.clear();
            }
        } else if (c != ' ' && c != '\t') {
            token += c;
        }
    }
    if (!token.empty())
        raw_items.push_back(trim(token));

    for (const auto& raw : raw_items) {
        IndexPropertyAccessor acc;
        if (!parsePropertyAccessor(raw, acc))
            return {};
        result.push_back(std::move(acc));
    }
    return result;
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

        // (var.prop1, var.prop2, ...) or (var::Label.prop1, ...)
        auto accessors = readPropertyList(iss);
        if (accessors.empty())
            return std::nullopt;
        stmt.accessors = std::move(accessors);
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
