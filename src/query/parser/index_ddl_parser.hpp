#pragma once

#include <optional>
#include <string>
#include <vector>

namespace eugraph {

struct IndexPropertyAccessor {
    // Weak accessor: property_name only.
    // Strong accessor: source_label + property_name.
    bool is_strong = false;
    std::string source_label;
    std::string property_name;
};

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
    std::vector<IndexPropertyAccessor> accessors;
};

class IndexDdlParser {
public:
    static std::optional<IndexDdlStatement> tryParse(const std::string& query);
};

} // namespace eugraph
