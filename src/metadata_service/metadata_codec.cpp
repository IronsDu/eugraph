#include "metadata_service/metadata_codec.hpp"

#include <cstring>
#include <stdexcept>

namespace eugraph {

// ==================== Primitive helpers ====================

void MetadataCodec::encodeU64(std::string& buf, uint64_t val) {
    uint8_t bytes[8];
    for (int i = 7; i >= 0; --i) {
        bytes[i] = static_cast<uint8_t>(val & 0xFF);
        val >>= 8;
    }
    buf.append(reinterpret_cast<char*>(bytes), 8);
}

uint64_t MetadataCodec::decodeU64(std::string_view data, size_t& offset) {
    if (offset + 8 > data.size())
        throw std::runtime_error("MetadataCodec: unexpected end of data decoding U64");
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | static_cast<uint8_t>(data[offset++]);
    }
    return val;
}

void MetadataCodec::encodeU16(std::string& buf, uint16_t val) {
    buf.push_back(static_cast<char>((val >> 8) & 0xFF));
    buf.push_back(static_cast<char>(val & 0xFF));
}

uint16_t MetadataCodec::decodeU16(std::string_view data, size_t& offset) {
    if (offset + 2 > data.size())
        throw std::runtime_error("MetadataCodec: unexpected end of data decoding U16");
    uint16_t val = (static_cast<uint8_t>(data[offset]) << 8) | static_cast<uint8_t>(data[offset + 1]);
    offset += 2;
    return val;
}

void MetadataCodec::encodeU8(std::string& buf, uint8_t val) {
    buf.push_back(static_cast<char>(val));
}

uint8_t MetadataCodec::decodeU8(std::string_view data, size_t& offset) {
    if (offset >= data.size())
        throw std::runtime_error("MetadataCodec: unexpected end of data decoding U8");
    return static_cast<uint8_t>(data[offset++]);
}

void MetadataCodec::encodeString(std::string& buf, const std::string& str) {
    encodeU16(buf, static_cast<uint16_t>(str.size()));
    buf.append(str);
}

std::string MetadataCodec::decodeString(std::string_view data, size_t& offset) {
    auto len = decodeU16(data, offset);
    if (offset + len > data.size())
        throw std::runtime_error("MetadataCodec: unexpected end of data decoding string");
    std::string result(data.substr(offset, len));
    offset += len;
    return result;
}

// ==================== PropertyValue ====================

void MetadataCodec::encodePropertyValue(std::string& buf, const PropertyValue& value) {
    // type_tag: variant index
    encodeU8(buf, static_cast<uint8_t>(value.index()));

    std::visit([&buf](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            // null — no data
        } else if constexpr (std::is_same_v<T, bool>) {
            encodeU8(buf, arg ? 1 : 0);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            encodeU64(buf, static_cast<uint64_t>(arg));
        } else if constexpr (std::is_same_v<T, double>) {
            uint64_t bits;
            std::memcpy(&bits, &arg, sizeof(double));
            encodeU64(buf, bits);
        } else if constexpr (std::is_same_v<T, std::string>) {
            encodeString(buf, arg);
        } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
            encodeU16(buf, static_cast<uint16_t>(arg.size()));
            for (auto v : arg)
                encodeU64(buf, static_cast<uint64_t>(v));
        } else if constexpr (std::is_same_v<T, std::vector<double>>) {
            encodeU16(buf, static_cast<uint16_t>(arg.size()));
            for (auto v : arg) {
                uint64_t bits;
                std::memcpy(&bits, &v, sizeof(double));
                encodeU64(buf, bits);
            }
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            encodeU16(buf, static_cast<uint16_t>(arg.size()));
            for (const auto& s : arg)
                encodeString(buf, s);
        }
    }, value);
}

PropertyValue MetadataCodec::decodePropertyValue(std::string_view data, size_t& offset) {
    auto tag = decodeU8(data, offset);
    switch (tag) {
    case 0: return std::monostate{};
    case 1: return decodeU8(data, offset) != 0;
    case 2: return static_cast<int64_t>(decodeU64(data, offset));
    case 3: {
        uint64_t bits = decodeU64(data, offset);
        double val;
        std::memcpy(&val, &bits, sizeof(double));
        return val;
    }
    case 4: return decodeString(data, offset);
    case 5: {
        auto count = decodeU16(data, offset);
        std::vector<int64_t> vec(count);
        for (uint16_t i = 0; i < count; ++i)
            vec[i] = static_cast<int64_t>(decodeU64(data, offset));
        return vec;
    }
    case 6: {
        auto count = decodeU16(data, offset);
        std::vector<double> vec(count);
        for (uint16_t i = 0; i < count; ++i) {
            uint64_t bits = decodeU64(data, offset);
            std::memcpy(&vec[i], &bits, sizeof(double));
        }
        return vec;
    }
    case 7: {
        auto count = decodeU16(data, offset);
        std::vector<std::string> vec(count);
        for (uint16_t i = 0; i < count; ++i)
            vec[i] = decodeString(data, offset);
        return vec;
    }
    default:
        throw std::runtime_error("MetadataCodec: unknown PropertyValue tag " + std::to_string(tag));
    }
}

// ==================== PropertyDef ====================

void MetadataCodec::encodePropertyDef(std::string& buf, const PropertyDef& prop) {
    encodeU16(buf, prop.id);
    encodeString(buf, prop.name);
    encodeU8(buf, static_cast<uint8_t>(prop.type));
    encodeU8(buf, prop.required ? 1 : 0);
    encodeU8(buf, prop.default_value.has_value() ? 1 : 0);
    if (prop.default_value.has_value()) {
        encodePropertyValue(buf, *prop.default_value);
    }
}

PropertyDef MetadataCodec::decodePropertyDef(std::string_view data, size_t& offset) {
    PropertyDef prop;
    prop.id = decodeU16(data, offset);
    prop.name = decodeString(data, offset);
    prop.type = static_cast<PropertyType>(decodeU8(data, offset));
    prop.required = decodeU8(data, offset) != 0;
    bool has_default = decodeU8(data, offset) != 0;
    if (has_default) {
        prop.default_value = decodePropertyValue(data, offset);
    }
    return prop;
}

// ==================== LabelDef ====================

std::string MetadataCodec::encodeLabelDef(const LabelDef& def) {
    std::string buf;
    encodeU16(buf, def.id);
    encodeString(buf, def.name);
    encodeU16(buf, static_cast<uint16_t>(def.properties.size()));
    for (const auto& prop : def.properties) {
        encodePropertyDef(buf, prop);
    }
    // indexes — not yet used, encode count=0
    encodeU16(buf, 0);
    return buf;
}

LabelDef MetadataCodec::decodeLabelDef(std::string_view data) {
    size_t offset = 0;
    LabelDef def;
    def.id = decodeU16(data, offset);
    def.name = decodeString(data, offset);
    auto prop_count = decodeU16(data, offset);
    def.properties.resize(prop_count);
    for (uint16_t i = 0; i < prop_count; ++i) {
        def.properties[i] = decodePropertyDef(data, offset);
    }
    // indexes — skip
    auto idx_count = decodeU16(data, offset);
    (void)idx_count;
    return def;
}

// ==================== EdgeLabelDef ====================

std::string MetadataCodec::encodeEdgeLabelDef(const EdgeLabelDef& def) {
    std::string buf;
    encodeU16(buf, def.id);
    encodeString(buf, def.name);
    encodeU16(buf, static_cast<uint16_t>(def.properties.size()));
    for (const auto& prop : def.properties) {
        encodePropertyDef(buf, prop);
    }
    encodeU8(buf, def.directed ? 1 : 0);
    return buf;
}

EdgeLabelDef MetadataCodec::decodeEdgeLabelDef(std::string_view data) {
    size_t offset = 0;
    EdgeLabelDef def;
    def.id = decodeU16(data, offset);
    def.name = decodeString(data, offset);
    auto prop_count = decodeU16(data, offset);
    def.properties.resize(prop_count);
    for (uint16_t i = 0; i < prop_count; ++i) {
        def.properties[i] = decodePropertyDef(data, offset);
    }
    def.directed = decodeU8(data, offset) != 0;
    return def;
}

// ==================== ID counters ====================

std::string MetadataCodec::encodeNextIds(VertexId next_vid, EdgeId next_eid, LabelId next_lid, EdgeLabelId next_elid) {
    std::string buf;
    encodeU64(buf, next_vid);
    encodeU64(buf, next_eid);
    encodeU16(buf, next_lid);
    encodeU16(buf, next_elid);
    return buf;
}

void MetadataCodec::decodeNextIds(std::string_view data, VertexId& next_vid, EdgeId& next_eid, LabelId& next_lid,
                                  EdgeLabelId& next_elid) {
    size_t offset = 0;
    next_vid = decodeU64(data, offset);
    next_eid = decodeU64(data, offset);
    next_lid = decodeU16(data, offset);
    next_elid = decodeU16(data, offset);
}

} // namespace eugraph
