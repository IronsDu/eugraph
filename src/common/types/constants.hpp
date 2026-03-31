#pragma once

#include <cstdint>

namespace eugraph {

// KV Key Prefix definitions (single byte)
constexpr uint8_t PREFIX_LABEL_REVERSE   = 0x4C; // 'L' - Vertex -> Labels
constexpr uint8_t PREFIX_LABEL_FORWARD   = 0x49; // 'I' - Label -> Vertices
constexpr uint8_t PREFIX_PK_FORWARD      = 0x50; // 'P' - pk -> vertex_id
constexpr uint8_t PREFIX_PK_REVERSE      = 0x52; // 'R' - vertex_id -> pk
constexpr uint8_t PREFIX_PROPERTY        = 0x58; // 'X' - Vertex property storage
constexpr uint8_t PREFIX_EDGE_INDEX      = 0x45; // 'E' - Edge adjacency index
constexpr uint8_t PREFIX_EDGE_PROPERTY   = 0x44; // 'D' - Edge property storage
constexpr uint8_t PREFIX_METADATA        = 0x4D; // 'M' - Metadata

} // namespace eugraph
