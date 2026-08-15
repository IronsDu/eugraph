#pragma once

#include <optional>
#include <string>
#include <vector>

namespace eugraph {

struct DatabaseDdlStatement {
    enum Type {
        CREATE_DATABASE,
        DROP_DATABASE,
        SHOW_DATABASES,
        SHOW_DATABASE,
        SHOW_PROCEDURES,
        SHOW_FUNCTIONS,
        SHOW_CURRENT_USER,
        SHOW_VECTOR_INDEXES,
        USE_GRAPH,
    };
    Type type;
    std::string name; // database/graph name
    bool yield_all = false;
};

class DatabaseDdlParser {
public:
    static std::optional<DatabaseDdlStatement> tryParse(const std::string& query);
};

} // namespace eugraph
