#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>

namespace eugraph {

// WiredTiger 全局表名常量
constexpr const char* TABLE_LABEL_REVERSE = "table:label_reverse";
constexpr const char* TABLE_PK_FORWARD = "table:pk_forward";
constexpr const char* TABLE_PK_REVERSE = "table:pk_reverse";
constexpr const char* TABLE_EDGE_INDEX = "table:edge_index";
constexpr const char* TABLE_METADATA = "table:metadata";

// 按 label_id 分表（dropLabel 时 drop）
inline std::string labelFwdTable(LabelId id) {
    return "table:label_fwd_" + std::to_string(id);
}
inline std::string vpropTable(LabelId id) {
    return "table:vprop_" + std::to_string(id);
}

// 按 edge_label_id 分表（dropEdgeLabel 时 drop）
inline std::string etypeTable(EdgeLabelId id) {
    return "table:etype_" + std::to_string(id);
}
inline std::string epropTable(EdgeLabelId id) {
    return "table:eprop_" + std::to_string(id);
}

// WiredTiger 表配置
constexpr const char* WT_TABLE_CONFIG = "key_format=u,value_format=u";

} // namespace eugraph
