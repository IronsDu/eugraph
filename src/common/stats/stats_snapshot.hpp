#pragma once
/// 持久化 GraphStatsSnapshot 的编码。

#include "common/stats/graph_stats.hpp"

#include <string>
#include <string_view>

namespace eugraph {

std::string encodeStatsSnapshot(const GraphStatsSnapshot& snapshot);
bool decodeStatsSnapshot(std::string_view bytes, GraphStatsSnapshot& out);

} // namespace eugraph
