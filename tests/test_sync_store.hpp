#pragma once

// 测试用 Sync store 包装：**连接 + 会话池**由测试自己拥有，store 只 bind ✓
//
// 生产代码里连接归 `WtConnectionStore`、事务归 `GraphSession`/`SessionPool`；测试既有的
// `store->beginTransaction()/commitTransaction(txn)` 习惯由本包装转成会话 API，
// 避免在每个用例里重复样板。**仅测试代码使用** ✓。

#include "storage/data/session_pool.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"
#include "storage/wt_connection_store.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace eugraph::test {

class TestSyncGraphDataStore : public SyncGraphDataStore {
public:
    explicit TestSyncGraphDataStore(uint32_t adjacency_cache_capacity = 0)
        : SyncGraphDataStore(adjacency_cache_capacity) {
        bind_connection(conn_);
        pool_ = std::make_unique<SessionPool>(16, [this]() { return conn_.make_session(); });
    }
    ~TestSyncGraphDataStore() override {
        active_.clear();
        auto_lease_.reset();
        pool_.reset();
        conn_.close();
    }

    TestSyncGraphDataStore(const TestSyncGraphDataStore&) = delete;
    TestSyncGraphDataStore& operator=(const TestSyncGraphDataStore&) = delete;

    bool open(const std::string& db_path) {
        if (!conn_.open(db_path))
            return false;
        return ensure_tables();
    }

    void close() {
        active_.clear();
        auto_lease_.reset();
        if (pool_)
            pool_->clear();
        conn_.close();
    }
    [[nodiscard]] bool isOpen() const {
        return conn_.is_open();
    }

    /// 写事务（测试习惯：显式 begin/commit ✓）
    [[nodiscard]] std::reference_wrapper<GraphSession> beginTransaction() {
        auto lease = pool_->acquire();
        if (!lease)
            throw std::runtime_error("TestSyncGraphDataStore: session pool exhausted");
        GraphSession& session = **lease;
        if (!session.begin_write())
            throw std::runtime_error("TestSyncGraphDataStore: begin_write failed");
        auto holder = std::make_unique<SessionLease>(std::move(*lease));
        active_.push_back(Active{&session, std::move(holder)});
        return std::ref(session);
    }

    bool commitTransaction(std::reference_wrapper<GraphSession> ref) {
        const bool ok = ref.get().commit().committed();
        release_active(ref.get());
        return ok;
    }
    bool rollbackTransaction(std::reference_wrapper<GraphSession> ref) {
        ref.get().rollback();
        release_active(ref.get());
        return true;
    }

    /// 无事务会话（autocommit 读：等价于「没有显式事务」的旧读路径 ✓）
    /// 每次调用视为一条新语句：清掉会话级 cursor 缓存（生产里池 put 时会清 ✓）。
    /// 不这么做，缓存 cursor 的读会停留在旧快照，读不到其它会话已提交的删除。
    [[nodiscard]] std::reference_wrapper<GraphSession> autoSession() {
        if (!auto_lease_)
            auto_lease_ = std::make_unique<SessionLease>(std::move(*pool_->acquire()));
        (**auto_lease_).end_statement();
        return std::ref(**auto_lease_);
    }

private:
    struct Active {
        GraphSession* session;
        std::unique_ptr<SessionLease> lease;
    };

    void release_active(GraphSession& session) {
        for (auto it = active_.begin(); it != active_.end(); ++it) {
            if (it->session == &session) {
                active_.erase(it); // ~SessionLease ⇒ 池 put ✓
                return;
            }
        }
    }

    WtConnectionStore conn_;
    std::unique_ptr<SessionPool> pool_;
    std::unique_ptr<SessionLease> auto_lease_;
    std::vector<Active> active_;
};

class TestSyncGraphMetaStore : public SyncGraphMetaStore {
public:
    TestSyncGraphMetaStore() {
        bind_connection(conn_);
    }
    ~TestSyncGraphMetaStore() override {
        conn_.close();
    }

    TestSyncGraphMetaStore(const TestSyncGraphMetaStore&) = delete;
    TestSyncGraphMetaStore& operator=(const TestSyncGraphMetaStore&) = delete;

    bool open(const std::string& db_path) {
        if (!conn_.open(db_path))
            return false;
        return ensure_tables();
    }

    void close() {
        conn_.close();
    }
    [[nodiscard]] bool isOpen() const {
        return conn_.is_open();
    }

private:
    WtConnectionStore conn_;
};

} // namespace eugraph::test
