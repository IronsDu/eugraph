#include <gtest/gtest.h>

#include "storage/kv/wiredtiger_store.hpp"
#include "storage/kv/key_codec.hpp"
#include "storage/kv/value_codec.hpp"

#include <filesystem>
#include <unistd.h>

using namespace eugraph;

class WiredTigerStoreTest : public ::testing::Test {
protected:
    std::string test_db_path_;
    std::unique_ptr<WiredTigerStore> store_;

    void SetUp() override {
        test_db_path_ = "/tmp/eugraph_test_" + std::to_string(getpid());
        store_ = std::make_unique<WiredTigerStore>(test_db_path_);
        ASSERT_TRUE(store_->open());
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(test_db_path_);
    }
};

// ==================== Basic KV Operations ====================

TEST_F(WiredTigerStoreTest, PutAndGet) {
    EXPECT_TRUE(store_->put("key1", "value1"));

    auto value = store_->get("key1");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "value1");
}

TEST_F(WiredTigerStoreTest, GetNonExistentKey) {
    auto value = store_->get("nonexistent");
    EXPECT_FALSE(value.has_value());
}

TEST_F(WiredTigerStoreTest, PutOverwrite) {
    store_->put("key1", "value1");
    store_->put("key1", "value2");

    auto value = store_->get("key1");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "value2");
}

TEST_F(WiredTigerStoreTest, Delete) {
    store_->put("key1", "value1");
    EXPECT_TRUE(store_->del("key1"));

    auto value = store_->get("key1");
    EXPECT_FALSE(value.has_value());
}

TEST_F(WiredTigerStoreTest, DeleteNonExistentKey) {
    // Deleting a non-existent key should not error
    EXPECT_TRUE(store_->del("nonexistent"));
}

TEST_F(WiredTigerStoreTest, BinaryKeyAndValue) {
    // Test with binary data (KV keys/values contain raw bytes)
    std::string bin_key(10, '\0');
    bin_key[0] = 'X';
    // Encode label_id=1 (BE) at offset 1-2
    bin_key[1] = 0;
    bin_key[2] = 1;

    std::string bin_val = ValueCodec::encode(int64_t{42});

    EXPECT_TRUE(store_->put(bin_key, bin_val));

    auto result = store_->get(bin_key);
    ASSERT_TRUE(result.has_value());

    auto decoded = ValueCodec::decode(result.value());
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded));
    EXPECT_EQ(std::get<int64_t>(decoded), 42);
}

// ==================== Prefix Scan ====================

TEST_F(WiredTigerStoreTest, PrefixScanBasic) {
    // Insert keys with a common prefix
    store_->put("prefix_a", "value_a");
    store_->put("prefix_b", "value_b");
    store_->put("prefix_c", "value_c");
    store_->put("other_1", "value_1");

    auto results = store_->prefixScan("prefix_");
    EXPECT_EQ(results.size(), 3u);
}

TEST_F(WiredTigerStoreTest, PrefixScanNoResults) {
    store_->put("key1", "value1");
    auto results = store_->prefixScan("nonexistent_");
    EXPECT_TRUE(results.empty());
}

TEST_F(WiredTigerStoreTest, PrefixScanWithCallback) {
    store_->put("pre_1", "val_1");
    store_->put("pre_2", "val_2");
    store_->put("pre_3", "val_3");
    store_->put("other", "val");

    int count = 0;
    store_->prefixScan("pre_", [&count](std::string_view key, std::string_view value) {
        count++;
        return true; // continue
    });
    EXPECT_EQ(count, 3);
}

TEST_F(WiredTigerStoreTest, PrefixScanEarlyStop) {
    store_->put("pre_1", "val_1");
    store_->put("pre_2", "val_2");
    store_->put("pre_3", "val_3");

    int count = 0;
    store_->prefixScan("pre_", [&count](std::string_view key, std::string_view value) {
        count++;
        return false; // stop after first
    });
    EXPECT_EQ(count, 1);
}

TEST_F(WiredTigerStoreTest, PrefixScanWithEncodedKeys) {
    // Use actual KV-encoded keys
    auto key1 = KeyCodec::encodePropertyKey(1, 1, 1);  // X|1|1|1
    auto key2 = KeyCodec::encodePropertyKey(1, 1, 2);  // X|1|1|2
    auto key3 = KeyCodec::encodePropertyKey(1, 1, 3);  // X|1|1|3
    auto key4 = KeyCodec::encodePropertyKey(2, 1, 1);  // X|2|1|1 (different label)

    store_->put(key1, ValueCodec::encode(std::string{"Alice"}));
    store_->put(key2, ValueCodec::encode(int64_t{30}));
    store_->put(key3, ValueCodec::encode(std::string{"alice@email.com"}));
    store_->put(key4, ValueCodec::encode(std::string{"Employee"}));

    // Scan all properties for label 1, vertex 1
    auto prefix = KeyCodec::encodePropertyPrefix(1, 1);
    auto results = store_->prefixScan(prefix);
    EXPECT_EQ(results.size(), 3u);

    // Verify the values can be decoded
    auto decoded = ValueCodec::decode(results[0].value);
    EXPECT_TRUE(std::holds_alternative<std::string>(decoded));
}

// ==================== Batch Operations ====================

TEST_F(WiredTigerStoreTest, PutBatch) {
    std::vector<std::pair<std::string, std::string>> batch = {
        {"key1", "value1"},
        {"key2", "value2"},
        {"key3", "value3"},
    };

    EXPECT_TRUE(store_->putBatch(batch));

    EXPECT_EQ(store_->get("key1").value(), "value1");
    EXPECT_EQ(store_->get("key2").value(), "value2");
    EXPECT_EQ(store_->get("key3").value(), "value3");
}

// ==================== Transaction Operations ====================

TEST_F(WiredTigerStoreTest, TransactionCommit) {
    auto* session = store_->beginTransaction();
    ASSERT_NE(session, nullptr);

    EXPECT_TRUE(store_->put(session, "txn_key1", "txn_value1"));
    EXPECT_TRUE(store_->put(session, "txn_key2", "txn_value2"));

    EXPECT_TRUE(store_->commitTransaction(session));

    // Data should be visible after commit
    EXPECT_EQ(store_->get("txn_key1").value(), "txn_value1");
    EXPECT_EQ(store_->get("txn_key2").value(), "txn_value2");
}

TEST_F(WiredTigerStoreTest, TransactionRollback) {
    // First insert a value outside the transaction
    store_->put("base_key", "base_value");

    auto* session = store_->beginTransaction();
    ASSERT_NE(session, nullptr);

    EXPECT_TRUE(store_->put(session, "txn_key", "txn_value"));
    EXPECT_TRUE(store_->rollbackTransaction(session));

    // Rolled-back data should not be visible
    EXPECT_FALSE(store_->get("txn_key").has_value());

    // Original data should still be there
    EXPECT_EQ(store_->get("base_key").value(), "base_value");
}

// ==================== Graph-Level Integration ====================

TEST_F(WiredTigerStoreTest, InsertVertexWithPropertyAndLabel) {
    // Simulate inserting a vertex with a label and property
    VertexId vid = 1;
    LabelId label_id = 1; // Person
    uint16_t prop_id = 1;  // name

    // 1. Insert label reverse index: L|{vid}|{label_id}
    auto label_key = KeyCodec::encodeLabelReverseKey(vid, label_id);
    EXPECT_TRUE(store_->put(label_key, ""));

    // 2. Insert label forward index: I|{label_id}|{vid}
    auto fwd_key = KeyCodec::encodeLabelForwardKey(label_id, vid);
    EXPECT_TRUE(store_->put(fwd_key, ""));

    // 3. Insert property: X|{label_id}|{vid}|{prop_id}
    auto prop_key = KeyCodec::encodePropertyKey(label_id, vid, prop_id);
    auto prop_val = ValueCodec::encode(std::string{"Alice"});
    EXPECT_TRUE(store_->put(prop_key, prop_val));

    // 4. Insert primary key index: P|{pk_value}
    auto pk_key = KeyCodec::encodePkForwardKey("alice@example.com");
    auto pk_val = ValueCodec::encodeU64(vid);
    EXPECT_TRUE(store_->put(pk_key, pk_val));

    // 5. Insert primary key reverse: R|{vid}
    auto rpk_key = KeyCodec::encodePkReverseKey(vid);
    EXPECT_TRUE(store_->put(rpk_key, "alice@example.com"));

    // Verify: get vertex labels
    auto label_prefix = KeyCodec::encodeLabelReversePrefix(vid);
    auto labels = store_->prefixScan(label_prefix);
    EXPECT_EQ(labels.size(), 1u);
    auto [decoded_vid, decoded_label] = KeyCodec::decodeLabelReverseKey(labels[0].key);
    EXPECT_EQ(decoded_vid, vid);
    EXPECT_EQ(decoded_label, label_id);

    // Verify: get vertex by label
    auto fwd_prefix = KeyCodec::encodeLabelForwardPrefix(label_id);
    auto vertices = store_->prefixScan(fwd_prefix);
    EXPECT_EQ(vertices.size(), 1u);

    // Verify: get property
    auto value = store_->get(prop_key);
    ASSERT_TRUE(value.has_value());
    auto decoded = ValueCodec::decode(value.value());
    ASSERT_TRUE(std::holds_alternative<std::string>(decoded));
    EXPECT_EQ(std::get<std::string>(decoded), "Alice");

    // Verify: lookup by primary key
    auto pk_result = store_->get(pk_key);
    ASSERT_TRUE(pk_result.has_value());
    EXPECT_EQ(ValueCodec::decodeU64(pk_result.value()), vid);
}

TEST_F(WiredTigerStoreTest, InsertEdge) {
    VertexId src = 1, dst = 2;
    EdgeLabelId edge_label = 1; // follows
    EdgeId edge_id = 100;
    uint64_t seq = 1;

    // 1. Insert edge index (out direction)
    KeyCodec::EdgeIndexKey out_key{src, Direction::OUT, edge_label, dst, seq};
    auto e_out = KeyCodec::encodeEdgeIndexKey(out_key);
    EXPECT_TRUE(store_->put(e_out, ValueCodec::encodeU64(edge_id)));

    // 2. Insert edge index (in direction)
    KeyCodec::EdgeIndexKey in_key{dst, Direction::IN, edge_label, src, seq};
    auto e_in = KeyCodec::encodeEdgeIndexKey(in_key);
    EXPECT_TRUE(store_->put(e_in, ValueCodec::encodeU64(edge_id)));

    // 3. Insert edge property
    auto d_key = KeyCodec::encodeEdgePropertyKey(edge_label, edge_id, 1); // prop_id=1 (since)
    EXPECT_TRUE(store_->put(d_key, ValueCodec::encode(int64_t{2020})));

    // Verify: scan out edges from src
    auto out_prefix = KeyCodec::encodeEdgeIndexPrefix(src, Direction::OUT, edge_label);
    auto out_edges = store_->prefixScan(out_prefix);
    EXPECT_EQ(out_edges.size(), 1u);
    auto decoded_out = KeyCodec::decodeEdgeIndexKey(out_edges[0].key);
    EXPECT_EQ(decoded_out.neighbor_id, dst);
    EXPECT_EQ(ValueCodec::decodeU64(out_edges[0].value), edge_id);

    // Verify: scan in edges to dst
    auto in_prefix = KeyCodec::encodeEdgeIndexPrefix(dst, Direction::IN, edge_label);
    auto in_edges = store_->prefixScan(in_prefix);
    EXPECT_EQ(in_edges.size(), 1u);

    // Verify: get edge property
    auto prop = store_->get(d_key);
    ASSERT_TRUE(prop.has_value());
    auto decoded_prop = ValueCodec::decode(prop.value());
    ASSERT_TRUE(std::holds_alternative<int64_t>(decoded_prop));
    EXPECT_EQ(std::get<int64_t>(decoded_prop), 2020);
}
