#include <gtest/gtest.h>

#include "storage/kv/value_codec.hpp"

using namespace eugraph;

class ValueCodecTest : public ::testing::Test {};

TEST_F(ValueCodecTest, EncodeDecodeNull) {
    PropertyValue val{std::monostate{}};
    auto encoded = ValueCodec::encode(val);
    EXPECT_EQ(encoded.size(), 1u);

    auto decoded = ValueCodec::decode(encoded);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(decoded));
}

TEST_F(ValueCodecTest, EncodeDecodeBool) {
    {
        PropertyValue val{true};
        auto encoded = ValueCodec::encode(val);
        auto decoded = ValueCodec::decode(encoded);
        ASSERT_TRUE(std::holds_alternative<bool>(decoded));
        EXPECT_EQ(std::get<bool>(decoded), true);
    }
    {
        PropertyValue val{false};
        auto encoded = ValueCodec::encode(val);
        auto decoded = ValueCodec::decode(encoded);
        ASSERT_TRUE(std::holds_alternative<bool>(decoded));
        EXPECT_EQ(std::get<bool>(decoded), false);
    }
}

TEST_F(ValueCodecTest, EncodeDecodeInt64) {
    PropertyValue val{int64_t{42}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
    EXPECT_EQ(std::get<int64_t>(decoded), 42);
}

TEST_F(ValueCodecTest, EncodeDecodeInt64Negative) {
    PropertyValue val{int64_t{-123456}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
    EXPECT_EQ(std::get<int64_t>(decoded), -123456);
}

TEST_F(ValueCodecTest, EncodeDecodeInt64Max) {
    PropertyValue val{int64_t{INT64_MAX}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
    EXPECT_EQ(std::get<int64_t>(decoded), INT64_MAX);
}

TEST_F(ValueCodecTest, EncodeDecodeDouble) {
    PropertyValue val{3.14159};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<double>(decoded));
    EXPECT_DOUBLE_EQ(std::get<double>(decoded), 3.14159);
}

TEST_F(ValueCodecTest, EncodeDecodeString) {
    PropertyValue val{std::string{"hello world"}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<std::string>(decoded));
    EXPECT_EQ(std::get<std::string>(decoded), "hello world");
}

TEST_F(ValueCodecTest, EncodeDecodeEmptyString) {
    PropertyValue val{std::string{""}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<std::string>(decoded));
    EXPECT_EQ(std::get<std::string>(decoded), "");
}

TEST_F(ValueCodecTest, EncodeDecodeInt64Array) {
    PropertyValue val{std::vector<int64_t>{1, 2, 3, -10, 100}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<std::vector<int64_t>>(decoded));
    EXPECT_EQ(std::get<std::vector<int64_t>>(decoded),
              (std::vector<int64_t>{1, 2, 3, -10, 100}));
}

TEST_F(ValueCodecTest, EncodeDecodeDoubleArray) {
    PropertyValue val{std::vector<double>{1.1, 2.2, 3.3}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<std::vector<double>>(decoded));
    auto& arr = std::get<std::vector<double>>(decoded);
    EXPECT_DOUBLE_EQ(arr[0], 1.1);
    EXPECT_DOUBLE_EQ(arr[1], 2.2);
    EXPECT_DOUBLE_EQ(arr[2], 3.3);
}

TEST_F(ValueCodecTest, EncodeDecodeStringArray) {
    PropertyValue val{std::vector<std::string>{"foo", "bar", "baz"}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::string>>(decoded));
    EXPECT_EQ(std::get<std::vector<std::string>>(decoded),
              (std::vector<std::string>{"foo", "bar", "baz"}));
}

TEST_F(ValueCodecTest, EncodeDecodeEmptyArray) {
    PropertyValue val{std::vector<int64_t>{}};
    auto encoded = ValueCodec::encode(val);
    auto decoded = ValueCodec::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<std::vector<int64_t>>(decoded));
    EXPECT_TRUE(std::get<std::vector<int64_t>>(decoded).empty());
}

TEST_F(ValueCodecTest, EncodeDecodeU64) {
    auto encoded = ValueCodec::encodeU64(12345);
    EXPECT_EQ(encoded.size(), 8u);
    EXPECT_EQ(ValueCodec::decodeU64(encoded), 12345u);
}

TEST_F(ValueCodecTest, EncodeDecodeU64Max) {
    auto encoded = ValueCodec::encodeU64(UINT64_MAX);
    EXPECT_EQ(ValueCodec::decodeU64(encoded), UINT64_MAX);
}

TEST_F(ValueCodecTest, RoundTripAllTypes) {
    // Verify all types can round-trip through encode/decode
    std::vector<PropertyValue> values = {
        std::monostate{},
        true,
        int64_t{42},
        3.14,
        std::string{"test"},
        std::vector<int64_t>{1, 2},
        std::vector<double>{1.0, 2.0},
        std::vector<std::string>{"a", "b"},
    };

    for (const auto& val : values) {
        auto encoded = ValueCodec::encode(val);
        auto decoded = ValueCodec::decode(encoded);
        // Verify the type is preserved
        EXPECT_EQ(val.index(), decoded.index());
    }
}
