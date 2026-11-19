#pragma once
/// 变更表（delta）的值编解码：`{op: u8, payload: bytes}`（设计 §7.1）。
///
/// 键复用索引键布局 `IndexKeyCodec::encodeIndexKey(values, entity_id)`，
/// 因此重放时可以**原样**把 delta 的 key 写到索引表（无需解码属性值）：
///   * PUT：`table_put(index_table, key, payload)`；
///   * DEL：`table_del(index_table, key)`（删不存在的条目是 no-op，幂等）。
///
/// 纯逻辑、无 WT 依赖，直接单测。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace eugraph {

enum class IndexDeltaOp : uint8_t {
    kPut = 0,
    kDelete = 1,
};

struct IndexDeltaValue {
    IndexDeltaOp op = IndexDeltaOp::kPut;
    std::string payload;
};

inline std::string encodeIndexDeltaValue(IndexDeltaOp op, std::string_view payload = {}) {
    std::string encoded;
    encoded.reserve(1 + payload.size());
    encoded.push_back(static_cast<char>(op));
    encoded.append(payload.data(), payload.size());
    return encoded;
}

/// 非法/空值返回 nullopt（调用方应让该批事务失败，不要静默跳过）。
inline std::optional<IndexDeltaValue> decodeIndexDeltaValue(std::string_view encoded) {
    if (encoded.empty())
        return std::nullopt;
    const auto raw_op = static_cast<uint8_t>(encoded.front());
    IndexDeltaValue value;
    if (raw_op == static_cast<uint8_t>(IndexDeltaOp::kPut)) {
        value.op = IndexDeltaOp::kPut;
    } else if (raw_op == static_cast<uint8_t>(IndexDeltaOp::kDelete)) {
        value.op = IndexDeltaOp::kDelete;
    } else {
        return std::nullopt;
    }
    value.payload.assign(encoded.data() + 1, encoded.size() - 1);
    return value;
}

} // namespace eugraph
