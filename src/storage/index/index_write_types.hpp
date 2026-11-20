#pragma once
/// 索引维护写入的**共享类型**（异步 applier 与同步批量路径共用，避免两份定义漂移）。
///
/// 语义见设计 I2/I3：一次写入对每个索引只有一个去向（delta 或索引表），
/// 或者被显式跳过/要求重试。

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {

struct IndexWriteItem {
    uint32_t index_id = 0;    ///< 0 ⇒ legacy（按 legacy_table 直写）
    std::string legacy_table; ///< registry 不可用时的直写目标
    std::vector<PropertyValue> values;
    uint64_t entity_id = 0;
    bool unique = false;
    bool is_delete = false;
    std::string payload; ///< 边索引邻接值（顶点为空）
};

enum class IndexWriteStatus : uint8_t {
    kApplied = 0,
    kSkipped = 1,
    kUniqueConflict = 2,
    kRetryLater = 3,
};

} // namespace eugraph
