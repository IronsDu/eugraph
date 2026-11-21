#pragma once
/// 同事务 stats journal 的编码：
///   key   = BE64(seq)
///   value = version(u8) + 三个 delta map
/// 使用固定小端布局；同一进程/架构内 journal 由数据 DB 持有。

#include "common/stats/graph_stats.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace eugraph {

std::string encodeStatsJournalKey(uint64_t seq);
std::string encodeStatsDelta(const StatsDelta& delta);
bool decodeStatsDelta(std::string_view bytes, StatsDelta& out);

} // namespace eugraph
