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
        USE_GRAPH,
    };
    Type type;
    std::string name; // database/graph name
};

class DatabaseDdlParser {
public:
    static std::optional<DatabaseDdlStatement> tryParse(const std::string& query);
};

} // namespace eugraph
