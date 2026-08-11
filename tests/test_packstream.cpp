#include <gtest/gtest.h>

#include "service/bolt/packstream/decoder.hpp"
#include "service/bolt/packstream/encoder.hpp"

using namespace eugraph::service::bolt::packstream;
using PS = PackStreamValueStorage;
// Use aliases to work around gtest macro comma issues
using PSMap = std::unordered_map<std::string, PackStreamValueStorage>;
using PSList = std::vector<PackStreamValueStorage>;

namespace {

Value decodeOne(const std::vector<uint8_t>& buf) {
    Decoder dec(buf.data(), buf.size());
    return dec.decode();
}

std::vector<uint8_t> encodeOne(const Value& v) {
    Encoder enc;
    enc.writeValue(v);
    return enc.release();
}

} // namespace

class PackStreamTest : public ::testing::Test {};

// ==================== Null ====================

TEST_F(PackStreamTest, EncodeDecodeNull) {
    Value val{std::monostate{}};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 1u);
    EXPECT_EQ(encoded[0], 0xC0);

    auto decoded = decodeOne(encoded);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(decoded));
}

// ==================== Boolean ====================

TEST_F(PackStreamTest, EncodeDecodeBoolTrue) {
    Value val{true};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 1u);
    EXPECT_EQ(encoded[0], 0xC3);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<bool>(decoded));
    EXPECT_EQ(std::get<bool>(decoded), true);
}

TEST_F(PackStreamTest, EncodeDecodeBoolFalse) {
    Value val{false};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 1u);
    EXPECT_EQ(encoded[0], 0xC2);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<bool>(decoded));
    EXPECT_EQ(std::get<bool>(decoded), false);
}

// ==================== Integer ====================

TEST_F(PackStreamTest, EncodeDecodeIntTinyPositive) {
    for (int64_t i = 0; i <= 127; i++) {
        auto encoded = encodeOne(Value{i});
        ASSERT_EQ(encoded.size(), 1u);
        auto decoded = decodeOne(encoded);
        ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
        EXPECT_EQ(std::get<int64_t>(decoded), i);
    }
}

TEST_F(PackStreamTest, EncodeDecodeIntTinyNegative) {
    for (int64_t i = -16; i <= -1; i++) {
        auto encoded = encodeOne(Value{i});
        ASSERT_EQ(encoded.size(), 1u);
        auto decoded = decodeOne(encoded);
        ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
        EXPECT_EQ(std::get<int64_t>(decoded), i);
    }
}

TEST_F(PackStreamTest, EncodeDecodeInt8) {
    Value val{int64_t{-128}};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 2u);
    EXPECT_EQ(encoded[0], 0xC8);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
    EXPECT_EQ(std::get<int64_t>(decoded), -128);
}

TEST_F(PackStreamTest, EncodeDecodeInt16) {
    Value val{int64_t{300}};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 3u);
    EXPECT_EQ(encoded[0], 0xC9);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
    EXPECT_EQ(std::get<int64_t>(decoded), 300);
}

TEST_F(PackStreamTest, EncodeDecodeInt32) {
    Value val{int64_t{100000}};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 5u);
    EXPECT_EQ(encoded[0], 0xCA);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
    EXPECT_EQ(std::get<int64_t>(decoded), 100000);
}

TEST_F(PackStreamTest, EncodeDecodeInt64) {
    Value val{int64_t{5000000000LL}};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 9u);
    EXPECT_EQ(encoded[0], 0xCB);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
    EXPECT_EQ(std::get<int64_t>(decoded), 5000000000LL);
}

// ==================== Float ====================

TEST_F(PackStreamTest, EncodeDecodeFloat) {
    Value val{3.14159};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 9u);
    EXPECT_EQ(encoded[0], 0xC1);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<double>(decoded));
    EXPECT_DOUBLE_EQ(std::get<double>(decoded), 3.14159);
}

// ==================== String ====================

TEST_F(PackStreamTest, EncodeDecodeTinyString) {
    std::string s = "hello";
    Value val{s};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded.size(), 1u + s.size());
    EXPECT_EQ(encoded[0], uint8_t(0x80 | s.size()));
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<std::string>(decoded));
    EXPECT_EQ(std::get<std::string>(decoded), s);
}

TEST_F(PackStreamTest, EncodeDecodeString8) {
    std::string s(200, 'x');
    Value val{s};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], 0xD0);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<std::string>(decoded));
    EXPECT_EQ(std::get<std::string>(decoded), s);
}

TEST_F(PackStreamTest, EncodeDecodeString16) {
    std::string s(500, 'y');
    Value val{s};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], 0xD1);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<std::string>(decoded));
    EXPECT_EQ(std::get<std::string>(decoded), s);
}

TEST_F(PackStreamTest, EncodeDecodeEmptyString) {
    std::string s;
    Value val{s};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], uint8_t(0x80));
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<std::string>(decoded));
    EXPECT_EQ(std::get<std::string>(decoded), s);
}

// ==================== Bytes ====================

TEST_F(PackStreamTest, EncodeDecodeBytes8) {
    std::vector<uint8_t> bytes = {0x01, 0x02, 0x03, 0xFF};
    Value val{bytes};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], 0xCC);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<std::vector<uint8_t>>(decoded));
    EXPECT_EQ(std::get<std::vector<uint8_t>>(decoded), bytes);
}

// ==================== List ====================

TEST_F(PackStreamTest, EncodeDecodeTinyList) {
    PSList list;
    list.push_back(PS{int64_t{1}});
    list.push_back(PS{int64_t{2}});
    list.push_back(PS{int64_t{3}});
    Value val{list};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], uint8_t(0x90 | 3));
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<PSList>(decoded));
    auto& dlist = std::get<PSList>(decoded);
    ASSERT_EQ(dlist.size(), 3u);
    EXPECT_EQ(std::get<int64_t>(dlist[0].value), 1);
    EXPECT_EQ(std::get<int64_t>(dlist[1].value), 2);
    EXPECT_EQ(std::get<int64_t>(dlist[2].value), 3);
}

TEST_F(PackStreamTest, EncodeDecodeEmptyList) {
    PSList list;
    Value val{list};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], uint8_t(0x90));
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<PSList>(decoded));
    EXPECT_TRUE(std::get<PSList>(decoded).empty());
}

TEST_F(PackStreamTest, EncodeDecodeNestedList) {
    PSList inner;
    inner.push_back(PS{int64_t{1}});
    inner.push_back(PS{int64_t{2}});

    PSList outer;
    outer.push_back(PS{inner});
    outer.push_back(PS{std::string{"x"}});

    Value val{outer};
    auto encoded = encodeOne(val);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<PSList>(decoded));
    auto& dlist = std::get<PSList>(decoded);
    ASSERT_EQ(dlist.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<PSList>(dlist[0].value));
    ASSERT_TRUE(std::holds_alternative<std::string>(dlist[1].value));
}

// ==================== Dictionary ====================

TEST_F(PackStreamTest, EncodeDecodeTinyDict) {
    PSMap dict;
    dict["name"] = PS{std::string{"Alice"}};
    dict["age"] = PS{int64_t{30}};
    Value val{dict};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], uint8_t(0xA0 | 2));
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<PSMap>(decoded));
    auto& ddict = std::get<PSMap>(decoded);
    ASSERT_EQ(ddict.size(), 2u);
    EXPECT_EQ(std::get<std::string>(ddict["name"].value), "Alice");
    EXPECT_EQ(std::get<int64_t>(ddict["age"].value), 30);
}

TEST_F(PackStreamTest, EncodeDecodeEmptyDict) {
    PSMap dict;
    Value val{dict};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], uint8_t(0xA0));
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<PSMap>(decoded));
    EXPECT_TRUE(std::get<PSMap>(decoded).empty());
}

// ==================== Structure ====================

TEST_F(PackStreamTest, EncodeDecodeTinyStruct) {
    PackStreamStruct st;
    st.tag = 0x01;
    st.fields.push_back(PS{std::string{"hello"}});
    st.fields.push_back(PS{int64_t{42}});
    Value val{st};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], uint8_t(0xB0 | 2));
    ASSERT_EQ(encoded[1], 0x01);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<PackStreamStruct>(decoded));
    auto& dst = std::get<PackStreamStruct>(decoded);
    EXPECT_EQ(dst.tag, 0x01);
    ASSERT_EQ(dst.fields.size(), 2u);
    EXPECT_EQ(std::get<std::string>(dst.fields[0].value), "hello");
    EXPECT_EQ(std::get<int64_t>(dst.fields[1].value), 42);
}

TEST_F(PackStreamTest, EncodeDecodeEmptyStruct) {
    PackStreamStruct st;
    st.tag = 0x02;
    Value val{st};
    auto encoded = encodeOne(val);
    ASSERT_EQ(encoded[0], uint8_t(0xB0));
    ASSERT_EQ(encoded[1], 0x02);
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<PackStreamStruct>(decoded));
    auto& dst = std::get<PackStreamStruct>(decoded);
    EXPECT_EQ(dst.tag, 0x02);
    EXPECT_TRUE(dst.fields.empty());
}

// ==================== Structure (large) ====================

TEST_F(PackStreamTest, EncodeDecodeStruct32) {
    // STRUCT_32 (0xDF) is triggered when field_count > 65535.
    // Use exactly 65536 fields to cross the STRUCT_16 threshold.
    constexpr size_t kFieldCount = 65536;

    PackStreamStruct st;
    st.tag = 0x01;
    for (size_t i = 0; i < kFieldCount; i++)
        st.fields.push_back(PS{int64_t{1}});

    Value val{st};
    auto encoded = encodeOne(val);

    // Verify STRUCT_32 marker (0xDF)
    ASSERT_EQ(encoded[0], 0xDF);
    // field_count as uint32 big-endian: 65536 = 0x00010000
    EXPECT_EQ(encoded[1], 0x00);
    EXPECT_EQ(encoded[2], 0x01);
    EXPECT_EQ(encoded[3], 0x00);
    EXPECT_EQ(encoded[4], 0x00);
    // tag byte
    EXPECT_EQ(encoded[5], 0x01);

    // Decode and verify
    auto decoded = decodeOne(encoded);
    ASSERT_TRUE(std::holds_alternative<PackStreamStruct>(decoded));
    auto& dst = std::get<PackStreamStruct>(decoded);
    EXPECT_EQ(dst.tag, 0x01);
    ASSERT_EQ(dst.fields.size(), kFieldCount);
    // Spot-check: every field is int64_t(1)
    EXPECT_EQ(std::get<int64_t>(dst.fields[0].value), 1);
    EXPECT_EQ(std::get<int64_t>(dst.fields[kFieldCount - 1].value), 1);
}

TEST_F(PackStreamTest, DecodeStructHeaderStruct32) {
    Encoder enc;
    enc.writeStructHeader(0x02, 65537);
    auto buf = enc.release();

    // Verify STRUCT_32 marker
    ASSERT_EQ(buf[0], 0xDF);

    Decoder dec(buf.data(), buf.size());
    auto [tag, count] = dec.decodeStructHeader();
    EXPECT_EQ(tag, 0x02);
    EXPECT_EQ(count, 65537u);
}

// ==================== DecodeError ====================

TEST_F(PackStreamTest, DecodeTruncatedData) {
    // Only a marker byte, no data follows for an INT_8
    std::vector<uint8_t> buf = {0xC8}; // INT_8 but missing value byte
    Decoder dec(buf.data(), buf.size());
    EXPECT_THROW({ dec.decode(); }, DecodeError);
}

// ==================== Encoder.reset ====================

TEST_F(PackStreamTest, EncoderReset) {
    Encoder enc;
    enc.writeInt(42);
    EXPECT_FALSE(enc.buffer().empty());
    enc.reset();
    EXPECT_TRUE(enc.buffer().empty());
}

// ==================== Decoder.decodeStructHeader ====================

TEST_F(PackStreamTest, DecodeStructHeader) {
    Encoder enc;
    enc.writeStructHeader(0x01, 3);
    auto buf = enc.release();

    Decoder dec(buf.data(), buf.size());
    auto [tag, count] = dec.decodeStructHeader();
    EXPECT_EQ(tag, 0x01);
    EXPECT_EQ(count, 3u);
}

// ==================== Decoder.skip ====================

TEST_F(PackStreamTest, DecoderSkip) {
    Encoder enc;
    enc.writeInt(1);
    enc.writeString("skip_me");
    enc.writeInt(2);
    auto buf = enc.release();

    Decoder dec(buf.data(), buf.size());
    auto first = dec.decode(); // 1
    ASSERT_TRUE(std::holds_alternative<int64_t>(first));
    EXPECT_EQ(std::get<int64_t>(first), 1);

    dec.skip(); // skip "skip_me"

    auto third = dec.decode(); // 2
    ASSERT_TRUE(std::holds_alternative<int64_t>(third));
    EXPECT_EQ(std::get<int64_t>(third), 2);
}

// ==================== Multiple values ====================

TEST_F(PackStreamTest, EncodeDecodeMultipleValues) {
    Encoder enc;
    enc.writeNull();
    enc.writeBoolean(true);
    enc.writeInt(-5);
    enc.writeFloat(1.5);
    enc.writeString("abc");
    auto buf = enc.release();

    Decoder dec(buf.data(), buf.size());
    auto v1 = dec.decode();
    EXPECT_TRUE(std::holds_alternative<std::monostate>(v1));
    auto v2 = dec.decode();
    ASSERT_TRUE(std::holds_alternative<bool>(v2));
    EXPECT_TRUE(std::get<bool>(v2));
    auto v3 = dec.decode();
    ASSERT_TRUE(std::holds_alternative<int64_t>(v3));
    EXPECT_EQ(std::get<int64_t>(v3), -5);
    auto v4 = dec.decode();
    ASSERT_TRUE(std::holds_alternative<double>(v4));
    EXPECT_DOUBLE_EQ(std::get<double>(v4), 1.5);
    auto v5 = dec.decode();
    ASSERT_TRUE(std::holds_alternative<std::string>(v5));
    EXPECT_EQ(std::get<std::string>(v5), "abc");
}
