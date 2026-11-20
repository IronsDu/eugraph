#pragma once
/// 事务提交时的 **schema 兼容性判定**（纯逻辑，可单测）。
///
/// 背景（设计 §10.2 / §10.3）：
///   * 每个事务在 BEGIN 时绑定 `SchemaPin{generation, compat_version}`；
///   * 新增标签/属性/索引是兼容变更（不阻塞写事务）；
///   * 删除标签/属性、改类型、改主键是不兼容变更：只有当写事务触碰了被删除/变更的元素时，
///     才中止该事务（元素级判定，而非"compat_version 变了就全中止"）。
///
/// 本环境尚未开放破坏性 DDL（DDL 面只有 CREATE），所以：
///   * `compat_version` 恒 0，`SchemaChangeLog` 只会空转；
///   * 判定逻辑与单测先就位，未来开放破坏性 DDL 时在提交路径接线即可。

#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace eugraph {

/// 写事务触碰到的 schema 元素（提交校验的"touched set"）。
struct SchemaElementRef {
    enum class Kind : uint8_t {
        Label = 0,
        Property = 1,
        Index = 2
    };

    Kind kind = Kind::Label;
    uint64_t label_id = 0; ///< Label / Property / Index 都挂在该标签下（边标签用高位命名空间，由调用方保证）
    uint16_t prop_id = 0;  ///< Property 使用
    uint32_t index_id = 0; ///< Index 使用

    static SchemaElementRef label(uint64_t id) {
        return SchemaElementRef{Kind::Label, id, 0, 0};
    }
    static SchemaElementRef property(uint64_t label, uint16_t prop) {
        return SchemaElementRef{Kind::Property, label, prop, 0};
    }
    static SchemaElementRef index(uint64_t label, uint32_t idx) {
        return SchemaElementRef{Kind::Index, label, 0, idx};
    }

    bool operator==(const SchemaElementRef& other) const {
        return kind == other.kind && label_id == other.label_id && prop_id == other.prop_id &&
               index_id == other.index_id;
    }
};

struct SchemaElementRefHash {
    size_t operator()(const SchemaElementRef& ref) const noexcept {
        size_t h = static_cast<size_t>(ref.kind);
        h = h * 1315423911u + static_cast<size_t>(ref.label_id);
        h = h * 1315423911u + static_cast<size_t>(ref.prop_id);
        h = h * 1315423911u + static_cast<size_t>(ref.index_id);
        return h;
    }
};

/// 提交校验的纯判定：
///   * pin 的 compat_version == 当前值 ⇒ 没有不兼容变更，直接放行；
///   * 否则：只要 touched 集合没有命中"pin 之后被移除的元素"，仍可提交。
inline bool commitSchemaCompatible(uint64_t pin_compat_version, uint64_t current_compat_version,
                                   bool touches_removed_element) noexcept {
    if (pin_compat_version == current_compat_version)
        return true;
    return !touches_removed_element;
}

/// 记录"不兼容变更删除/改写的元素"，供提交校验的 touched 判定使用。
/// 只保留内存（事务 pin 不跨重启，重启后不存在旧事务）。
class SchemaChangeLog {
public:
    void recordRemoval(uint64_t compat_version, SchemaElementRef element) {
        std::lock_guard<std::mutex> lock(mu_);
        removals_.push_back(Removal{compat_version, element});
    }

    /// 是否存在"版本 > pin_compat_version"且命中 touched 的移除记录。
    bool touchesRemovedSince(const std::vector<SchemaElementRef>& touched, uint64_t pin_compat_version) const {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& removal : removals_) {
            if (removal.compat_version <= pin_compat_version)
                continue;
            for (const auto& element : touched) {
                if (element == removal.element)
                    return true;
            }
        }
        return false;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return removals_.size();
    }

private:
    struct Removal {
        uint64_t compat_version = 0;
        SchemaElementRef element;
    };

    mutable std::mutex mu_;
    std::vector<Removal> removals_;
};

} // namespace eugraph
