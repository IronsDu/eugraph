#pragma once
/// 变更表（delta）的值编码：`{ op: 1 字节, payload }`。
/// 键沿用索引键编解码器（`IndexKeyCodec::encodeIndexKey(values, entity_id)` ⇒ **索引键在前、实体 id 在后**），
/// 因此重放时可以**按键序**扫描、相同键相邻（唯一性自查只需比相邻项），并且天然按 (键, 实体) 去重。
/// 设计依据：docs/storage/online-index-build-design.md §5.0。
#include <cstdint>
#include <string>
#include <string_view>

namespace eugraph {

inline constexpr char kDeltaOpPut = 'P';
inline constexpr char kDeltaOpDel = 'D';

/// 编码：op（1 字节）+ payload（边索引的邻接值；顶点索引通常为空）
inline std::string encodeDeltaValue(bool is_delete, std::string_view payload = {}) {
    std::string out;
    out.reserve(payload.size() + 1);
    out.push_back(is_delete ? kDeltaOpDel : kDeltaOpPut);
    out.append(payload.data(), payload.size());
    return out;
}

/// 解码。非法/空值 ⇒ 返回 false（调用方应视为损坏并跳过）
inline bool decodeDeltaValue(std::string_view raw, bool& is_delete, std::string_view& payload) {
    if (raw.empty())
        return false;
    is_delete = (raw[0] == kDeltaOpDel);
    payload = raw.substr(1);
    return true;
}

} // namespace eugraph
