#pragma once

#include "common/types/graph_types.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace eugraph {

/// Codec for the bounded out-edge cache blob in vertex_data.
///
/// Layout (little-endian values):
///   {state:u8}
///   repeated count times, fixed 32-byte entries:
///     {edge_label:u16}{reserved0:u16}{reserved1:u32}
///     {neighbor_id:u64}{seq:u64}{edge_id:u64}
///
/// There is no count field: count = (blob.size() - 1) / 32.
/// Fixed entry width keeps deletion a fixed-size memmove and lets readers
/// derive the count from the value length.
class AdjacencyCodec {
public:
    static constexpr size_t kHeaderSize = 1;
    static constexpr size_t kEntrySize = 32;

    static std::string encode(const OutEdgeCache& cache);

    /// nullopt for malformed blobs (bad state/length/reserved bytes).
    static std::optional<OutEdgeCache> decode(std::string_view data);
};

} // namespace eugraph
