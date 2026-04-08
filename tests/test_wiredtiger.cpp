#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>

#include <wiredtiger.h>

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_wt_test_" + std::to_string(::getpid()) + "_" +
           ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

// Helper to create a WT_ITEM from a string
WT_ITEM makeItem(const std::string& s) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = s.data();
    item.size = s.size();
    return item;
}

class WiredTigerBasicTest : public ::testing::Test {
protected:
    std::string db_path_;
    WT_CONNECTION* conn_ = nullptr;
    WT_SESSION* session_ = nullptr;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);
        std::filesystem::create_directories(db_path_);
    }

    void TearDown() override {
        if (session_) {
            session_->close(session_, nullptr);
            session_ = nullptr;
        }
        if (conn_) {
            conn_->close(conn_, nullptr);
            conn_ = nullptr;
        }
        std::filesystem::remove_all(db_path_);
    }
};

// Test 1: Open and close a WiredTiger database
TEST_F(WiredTigerBasicTest, OpenAndClose) {
    int ret = wiredtiger_open(db_path_.c_str(), nullptr, "create", &conn_);
    ASSERT_EQ(ret, 0) << "Failed to open WiredTiger database";
    ASSERT_NE(conn_, nullptr);

    ret = conn_->close(conn_, nullptr);
    ASSERT_EQ(ret, 0) << "Failed to close WiredTiger database";
    conn_ = nullptr;
}

// Test 2: Use raw byte format (key_format=u,value_format=u) for KV operations
TEST_F(WiredTigerBasicTest, PutAndGetRawBytes) {
    int ret = wiredtiger_open(db_path_.c_str(), nullptr, "create", &conn_);
    ASSERT_EQ(ret, 0);

    ret = conn_->open_session(conn_, nullptr, nullptr, &session_);
    ASSERT_EQ(ret, 0);

    ret = session_->create(session_, "table:kv_raw", "key_format=u,value_format=u");
    ASSERT_EQ(ret, 0);

    WT_CURSOR* cursor = nullptr;
    ret = session_->open_cursor(session_, "table:kv_raw", nullptr, nullptr, &cursor);
    ASSERT_EQ(ret, 0);

    // Insert
    std::string key_str = "hello";
    std::string val_str = "world";
    WT_ITEM key_item = makeItem(key_str);
    WT_ITEM val_item = makeItem(val_str);
    cursor->set_key(cursor, &key_item);
    cursor->set_value(cursor, &val_item);
    ret = cursor->insert(cursor);
    ASSERT_EQ(ret, 0);

    // Retrieve
    cursor->set_key(cursor, &key_item);
    ret = cursor->search(cursor);
    ASSERT_EQ(ret, 0);

    WT_ITEM got_val;
    std::memset(&got_val, 0, sizeof(got_val));
    cursor->get_value(cursor, &got_val);
    std::string result(static_cast<const char*>(got_val.data), got_val.size);
    EXPECT_EQ(result, "world");

    cursor->close(cursor);
}

// Test 3: Use string format (key_format=S,value_format=S) for simpler API
TEST_F(WiredTigerBasicTest, PutAndGetStringFormat) {
    int ret = wiredtiger_open(db_path_.c_str(), nullptr, "create", &conn_);
    ASSERT_EQ(ret, 0);

    ret = conn_->open_session(conn_, nullptr, nullptr, &session_);
    ASSERT_EQ(ret, 0);

    ret = session_->create(session_, "table:kv_str", "key_format=S,value_format=S");
    ASSERT_EQ(ret, 0);

    WT_CURSOR* cursor = nullptr;
    ret = session_->open_cursor(session_, "table:kv_str", nullptr, nullptr, &cursor);
    ASSERT_EQ(ret, 0);

    // Insert
    cursor->set_key(cursor, "hello");
    cursor->set_value(cursor, "world");
    ret = cursor->insert(cursor);
    ASSERT_EQ(ret, 0);

    // Retrieve
    cursor->set_key(cursor, "hello");
    ret = cursor->search(cursor);
    ASSERT_EQ(ret, 0);

    const char* got_val = nullptr;
    cursor->get_value(cursor, &got_val);
    ASSERT_NE(got_val, nullptr);
    EXPECT_EQ(std::string(got_val), "world");

    cursor->close(cursor);
}

// Test 4: Transaction rollback
TEST_F(WiredTigerBasicTest, TransactionRollback) {
    int ret = wiredtiger_open(db_path_.c_str(), nullptr, "create", &conn_);
    ASSERT_EQ(ret, 0);

    ret = conn_->open_session(conn_, nullptr, nullptr, &session_);
    ASSERT_EQ(ret, 0);

    ret = session_->create(session_, "table:txn_test", "key_format=S,value_format=S");
    ASSERT_EQ(ret, 0);

    WT_CURSOR* cursor = nullptr;
    ret = session_->open_cursor(session_, "table:txn_test", nullptr, nullptr, &cursor);
    ASSERT_EQ(ret, 0);

    // Begin transaction, insert, then rollback
    ret = session_->begin_transaction(session_, "isolation=snapshot");
    ASSERT_EQ(ret, 0);

    cursor->set_key(cursor, "txn_key");
    cursor->set_value(cursor, "txn_val");
    cursor->insert(cursor);

    ret = session_->rollback_transaction(session_, nullptr);
    ASSERT_EQ(ret, 0);

    // Verify the key does not exist
    cursor->set_key(cursor, "txn_key");
    ret = cursor->search(cursor);
    EXPECT_EQ(ret, WT_NOTFOUND);

    cursor->close(cursor);
}

// Test 5: Transaction commit
TEST_F(WiredTigerBasicTest, TransactionCommit) {
    int ret = wiredtiger_open(db_path_.c_str(), nullptr, "create", &conn_);
    ASSERT_EQ(ret, 0);

    ret = conn_->open_session(conn_, nullptr, nullptr, &session_);
    ASSERT_EQ(ret, 0);

    ret = session_->create(session_, "table:txn_commit", "key_format=S,value_format=S");
    ASSERT_EQ(ret, 0);

    WT_CURSOR* cursor = nullptr;
    ret = session_->open_cursor(session_, "table:txn_commit", nullptr, nullptr, &cursor);
    ASSERT_EQ(ret, 0);

    ret = session_->begin_transaction(session_, "isolation=snapshot");
    ASSERT_EQ(ret, 0);

    cursor->set_key(cursor, "commit_key");
    cursor->set_value(cursor, "commit_val");
    cursor->insert(cursor);

    ret = session_->commit_transaction(session_, nullptr);
    ASSERT_EQ(ret, 0);

    // Verify the key exists
    cursor->set_key(cursor, "commit_key");
    ret = cursor->search(cursor);
    ASSERT_EQ(ret, 0);

    const char* got_val = nullptr;
    cursor->get_value(cursor, &got_val);
    ASSERT_NE(got_val, nullptr);
    EXPECT_EQ(std::string(got_val), "commit_val");

    cursor->close(cursor);
}

// Test 6: Prefix scan using search_near
TEST_F(WiredTigerBasicTest, PrefixScan) {
    int ret = wiredtiger_open(db_path_.c_str(), nullptr, "create", &conn_);
    ASSERT_EQ(ret, 0);

    ret = conn_->open_session(conn_, nullptr, nullptr, &session_);
    ASSERT_EQ(ret, 0);

    ret = session_->create(session_, "table:scan_test", "key_format=S,value_format=S");
    ASSERT_EQ(ret, 0);

    WT_CURSOR* cursor = nullptr;
    ret = session_->open_cursor(session_, "table:scan_test", nullptr, nullptr, &cursor);
    ASSERT_EQ(ret, 0);

    // Insert keys with different prefixes
    cursor->set_key(cursor, "A|1");
    cursor->set_value(cursor, "val_a1");
    cursor->insert(cursor);
    cursor->set_key(cursor, "A|2");
    cursor->set_value(cursor, "val_a2");
    cursor->insert(cursor);
    cursor->set_key(cursor, "A|3");
    cursor->set_value(cursor, "val_a3");
    cursor->insert(cursor);
    cursor->set_key(cursor, "B|1");
    cursor->set_value(cursor, "val_b1");
    cursor->insert(cursor);

    // Scan all keys with prefix "A|"
    std::string prefix = "A|";
    cursor->set_key(cursor, prefix.c_str());
    int exact = 0;
    ret = cursor->search_near(cursor, &exact);

    int count = 0;
    if (ret != 0 || exact < 0) {
        ret = cursor->next(cursor);
    }

    while (ret == 0) {
        const char* got_key = nullptr;
        cursor->get_key(cursor, &got_key);
        if (!std::string(got_key).starts_with(prefix))
            break;
        count++;
        ret = cursor->next(cursor);
    }

    EXPECT_EQ(count, 3);
    cursor->close(cursor);
}

// Test 7: Delete operation
TEST_F(WiredTigerBasicTest, DeleteKey) {
    int ret = wiredtiger_open(db_path_.c_str(), nullptr, "create", &conn_);
    ASSERT_EQ(ret, 0);

    ret = conn_->open_session(conn_, nullptr, nullptr, &session_);
    ASSERT_EQ(ret, 0);

    ret = session_->create(session_, "table:del_test", "key_format=S,value_format=S");
    ASSERT_EQ(ret, 0);

    WT_CURSOR* cursor = nullptr;
    ret = session_->open_cursor(session_, "table:del_test", nullptr, nullptr, &cursor);
    ASSERT_EQ(ret, 0);

    cursor->set_key(cursor, "to_delete");
    cursor->set_value(cursor, "some_value");
    cursor->insert(cursor);

    // Delete
    cursor->set_key(cursor, "to_delete");
    ret = cursor->remove(cursor);
    ASSERT_EQ(ret, 0);

    // Verify
    cursor->set_key(cursor, "to_delete");
    ret = cursor->search(cursor);
    EXPECT_EQ(ret, WT_NOTFOUND);

    cursor->close(cursor);
}

} // anonymous namespace
