#pragma once

#include <optional>
#include <string>
#include <vector>

namespace eugraph {

struct IndexDdlStatement {
    enum Type {
        CREATE_VERTEX_INDEX,
        CREATE_EDGE_INDEX,
        DROP_INDEX,
        SHOW_INDEXES,
        SHOW_INDEX
    };
    Type type;
    bool unique = false;
    std::string index_name;
    std::string label_name;
    std::vector<std::string> property_names;
};

class IndexDdlParser {
public:
    static std::optional<IndexDdlStatement> tryParse(const std::string& query);
};

} // namespace eugraph
