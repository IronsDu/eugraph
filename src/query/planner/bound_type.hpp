#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace eugraph {
namespace binder {

enum class BoundTypeKind : uint8_t {
    BOOL,
    INT64,
    DOUBLE,
    STRING,
    VERTEX,
    EDGE,
    PATH,
    LIST,      // List<T>
    MAP,       // Map<K, V>
    ANY,       // 多标签弱类型访问，运行时类型不确定
    NULL_TYPE, // 字面量 NULL
};

struct BoundType {
    BoundTypeKind kind = BoundTypeKind::NULL_TYPE;
    std::unique_ptr<BoundType> element_type;   // only for LIST
    std::unique_ptr<BoundType> map_value_type; // only for MAP

    BoundType() = default;
    BoundType(BoundTypeKind k, std::unique_ptr<BoundType> elem) : kind(k), element_type(std::move(elem)) {}
    BoundType(BoundTypeKind k, std::unique_ptr<BoundType> key, std::unique_ptr<BoundType> val)
        : kind(k), element_type(std::move(key)), map_value_type(std::move(val)) {}

    // Copy: deep-clone the element_type tree
    BoundType(const BoundType& other) : kind(other.kind) {
        if (other.element_type)
            element_type = std::make_unique<BoundType>(*other.element_type);
        if (other.map_value_type)
            map_value_type = std::make_unique<BoundType>(*other.map_value_type);
    }
    BoundType& operator=(const BoundType& other) {
        if (this != &other) {
            kind = other.kind;
            element_type = other.element_type ? std::make_unique<BoundType>(*other.element_type) : nullptr;
            map_value_type = other.map_value_type ? std::make_unique<BoundType>(*other.map_value_type) : nullptr;
        }
        return *this;
    }

    // Move
    BoundType(BoundType&&) = default;
    BoundType& operator=(BoundType&&) = default;

    static BoundType Bool() {
        return {BoundTypeKind::BOOL, nullptr};
    }
    static BoundType Int64() {
        return {BoundTypeKind::INT64, nullptr};
    }
    static BoundType Double() {
        return {BoundTypeKind::DOUBLE, nullptr};
    }
    static BoundType String() {
        return {BoundTypeKind::STRING, nullptr};
    }
    static BoundType Vertex() {
        return {BoundTypeKind::VERTEX, nullptr};
    }
    static BoundType Edge() {
        return {BoundTypeKind::EDGE, nullptr};
    }
    static BoundType Path() {
        return {BoundTypeKind::PATH, nullptr};
    }
    static BoundType Any() {
        return {BoundTypeKind::ANY, nullptr};
    }
    static BoundType Null() {
        return {BoundTypeKind::NULL_TYPE, nullptr};
    }
    static BoundType List(BoundType elem) {
        return {BoundTypeKind::LIST, std::make_unique<BoundType>(std::move(elem))};
    }
    static BoundType Map(BoundType key, BoundType val) {
        return {BoundTypeKind::MAP, std::make_unique<BoundType>(std::move(key)),
                std::make_unique<BoundType>(std::move(val))};
    }

    bool operator==(const BoundType& other) const {
        if (kind != other.kind)
            return false;
        if (kind == BoundTypeKind::LIST) {
            if (!element_type && !other.element_type)
                return true;
            if (!element_type || !other.element_type)
                return false;
            return *element_type == *other.element_type;
        }
        if (kind == BoundTypeKind::MAP) {
            bool key_eq = element_type && other.element_type ? *element_type == *other.element_type
                                                             : element_type == other.element_type;
            bool val_eq = map_value_type && other.map_value_type ? *map_value_type == *other.map_value_type
                                                                 : map_value_type == other.map_value_type;
            return key_eq && val_eq;
        }
        return true;
    }
    bool operator!=(const BoundType& other) const {
        return !(*this == other);
    }

    std::string toString() const {
        switch (kind) {
        case BoundTypeKind::BOOL:
            return "BOOL";
        case BoundTypeKind::INT64:
            return "INT64";
        case BoundTypeKind::DOUBLE:
            return "DOUBLE";
        case BoundTypeKind::STRING:
            return "STRING";
        case BoundTypeKind::VERTEX:
            return "VERTEX";
        case BoundTypeKind::EDGE:
            return "EDGE";
        case BoundTypeKind::PATH:
            return "PATH";
        case BoundTypeKind::LIST:
            return "LIST<" + (element_type ? element_type->toString() : "?") + ">";
        case BoundTypeKind::MAP:
            return "MAP<" + (element_type ? element_type->toString() : "?") + ", " +
                   (map_value_type ? map_value_type->toString() : "?") + ">";
        case BoundTypeKind::ANY:
            return "ANY";
        case BoundTypeKind::NULL_TYPE:
            return "NULL";
        }
        return "UNKNOWN";
    }

    /// 隐式转换检查：this 是否可以隐式转换为 target
    bool canCastTo(const BoundType& target) const {
        return implicitCastCost(target) >= 0;
    }

    /// 隐式转换代价：返回整数代价，-1 表示不可转换。
    /// 代价用于函数重载解析，选择总代价最低的重载。
    ///   0 = 完全匹配
    ///   1 = INT64→DOUBLE, NULL→具体类型, ANY↔具体类型
    int implicitCastCost(const BoundType& target) const {
        if (*this == target)
            return 0;
        // INT64 -> DOUBLE
        if (kind == BoundTypeKind::INT64 && target.kind == BoundTypeKind::DOUBLE)
            return 1;
        // NULL -> anything
        if (kind == BoundTypeKind::NULL_TYPE)
            return 1;
        // LIST -> LIST with compatible elements
        if (kind == BoundTypeKind::LIST && target.kind == BoundTypeKind::LIST) {
            if (!element_type || !target.element_type)
                return 1;
            return element_type->implicitCastCost(*target.element_type);
        }
        // MAP -> MAP with compatible key/value types
        if (kind == BoundTypeKind::MAP && target.kind == BoundTypeKind::MAP) {
            int key_cost = 0;
            int val_cost = 0;
            if (element_type && target.element_type)
                key_cost = element_type->implicitCastCost(*target.element_type);
            if (map_value_type && target.map_value_type)
                val_cost = map_value_type->implicitCastCost(*target.map_value_type);
            if (key_cost < 0 || val_cost < 0)
                return -1;
            return key_cost + val_cost;
        }
        // ANY -> anything or anything -> ANY (runtime checked)
        if (kind == BoundTypeKind::ANY || target.kind == BoundTypeKind::ANY)
            return 1;
        return -1;
    }

    /// 合并两个类型：产生它们的公共超类型（用于弱类型属性访问）
    /// 相同类型 → 该类型；不同 → ANY；任一 NULL → 另一
    static BoundType merge(const BoundType& a, const BoundType& b) {
        if (a.kind == BoundTypeKind::NULL_TYPE)
            return clone(b);
        if (b.kind == BoundTypeKind::NULL_TYPE)
            return clone(a);
        if (a == b)
            return clone(a);
        // INT64 + DOUBLE → DOUBLE
        if ((a.kind == BoundTypeKind::INT64 && b.kind == BoundTypeKind::DOUBLE) ||
            (a.kind == BoundTypeKind::DOUBLE && b.kind == BoundTypeKind::INT64))
            return Double();
        return Any();
    }

    static BoundType clone(const BoundType& src) {
        BoundType result;
        result.kind = src.kind;
        if (src.element_type)
            result.element_type = std::make_unique<BoundType>(clone(*src.element_type));
        if (src.map_value_type)
            result.map_value_type = std::make_unique<BoundType>(clone(*src.map_value_type));
        return result;
    }
};

} // namespace binder
} // namespace eugraph
