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
        // DESCRIBE family: schema introspection of the *current* graph. Unlike the
        // SHOW statements above (database-level, always answered from the default
        // graph), these are graph-scoped and must see the selected graph.
        DESCRIBE_LABELS,        // DESCRIBE LABELS
        DESCRIBE_RELATIONSHIPS, // DESCRIBE RELATIONSHIPS
        DESCRIBE_LABEL,         // DESCRIBE LABEL <name>
        DESCRIBE_RELATIONSHIP,  // DESCRIBE RELATIONSHIP <name>
    };
    Type type;
    std::string name; // database/graph name, or the label/relationship type to describe
    bool yield_all = false;
};

class DatabaseDdlParser {
public:
    static std::optional<DatabaseDdlStatement> tryParse(const std::string& query);
};

} // namespace eugraph
