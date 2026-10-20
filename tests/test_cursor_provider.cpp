// cursor 所有权边界（CursorHandle / CursorProvider）测试：
// 句柄/假 provider 不依赖 WT；GraphSession 注入 provider 用真实 WT 连接。
#include "storage/data/cursor_provider.hpp"
#include "storage/data/graph_session.hpp"
#include "storage/wt_connection_store.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {
using eugraph::CursorHandle;
using eugraph::CursorProvider;
using eugraph::GraphSession;
using eugraph::WtConnectionStore;
using eugraph::WtCursor;

/// 记录调用的假 provider：返回的 cursor 故意无效（default WtCursor）⇒ 句柄也无效 ✓
class FakeCursorProvider final : public CursorProvider {
public:
    [[nodiscard]] CursorHandle acquire(const std::string& table) override {
        ++acquires;
        last_table = table;
        return CursorHandle::borrow(*this, borrowed_cursor);
    }
    [[nodiscard]] CursorHandle acquire_scan(const std::string& table) override {
        ++scans;
        last_table = table;
        return CursorHandle::own(std::make_unique<WtCursor>());
    }
    void clear() noexcept override {
        ++clears;
    }
    void release(WtCursor& /*cursor*/) noexcept override {
        ++releases;
    }

    int acquires = 0;
    int scans = 0;
    int clears = 0;
    int releases = 0;
    std::string last_table;
    WtCursor borrowed_cursor; // default-constructed ⇒ invalid ✓
};

class GraphSessionCursorProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = std::filesystem::current_path() / ("eugraph_cursor_" + std::string(info->name()));
        std::filesystem::remove_all(dir_);
        ASSERT_TRUE(conn_.open(dir_.string()));
    }
    void TearDown() override {
        conn_.close();
        std::filesystem::remove_all(dir_);
    }
    std::filesystem::path dir_;
    WtConnectionStore conn_;
};
} // namespace

TEST(CursorHandle, DefaultIsInvalidAndResetIsIdempotent) {
    CursorHandle handle;
    EXPECT_FALSE(handle.valid());
    EXPECT_EQ(handle.get(), nullptr);
    handle.reset();
    handle.reset();
    EXPECT_FALSE(handle.valid());
}

TEST(CursorHandle, BorrowedHandleReturnsToProviderOnReset) {
    FakeCursorProvider provider;
    {
        CursorHandle handle = CursorHandle::borrow(provider, provider.borrowed_cursor);
        EXPECT_FALSE(handle.valid()) << "fake cursor 无效 ⇒ 打开失败可见 ✓";
        EXPECT_EQ(provider.releases, 0);
    } // 析构 ⇒ 归还 ✓
    EXPECT_EQ(provider.releases, 1) << "borrowed 句柄析构只归还、不 close ✓";
}

TEST(CursorHandle, ResetIsIdempotentAndDoesNotReturnTwice) {
    FakeCursorProvider provider;
    CursorHandle handle = CursorHandle::borrow(provider, provider.borrowed_cursor);
    handle.reset();
    handle.reset();
    EXPECT_EQ(provider.releases, 1) << "重复 reset 只归还一次 ✓";
    EXPECT_FALSE(handle.valid());
}

TEST(CursorHandle, MoveTransfersOwnershipExactlyOnce) {
    FakeCursorProvider provider;
    {
        CursorHandle first = CursorHandle::borrow(provider, provider.borrowed_cursor);
        CursorHandle second(std::move(first));
        EXPECT_FALSE(first.valid());
        first.reset(); // moved-from：不得归还 ✓
        EXPECT_EQ(provider.releases, 0);
        CursorHandle third;
        third = std::move(second);
        EXPECT_EQ(provider.releases, 0);
    }
    EXPECT_EQ(provider.releases, 1) << "只有一个句柄负责归还 ✓";
}

TEST(CursorHandle, MoveAssignmentReleasesPreviousBorrowedHandle) {
    FakeCursorProvider provider;
    CursorHandle a = CursorHandle::borrow(provider, provider.borrowed_cursor);
    CursorHandle b = CursorHandle::borrow(provider, provider.borrowed_cursor);
    a = std::move(b); // 覆盖 a ⇒ 先归还 a ✓
    EXPECT_EQ(provider.releases, 1);
    a.reset();
    EXPECT_EQ(provider.releases, 2);
}

TEST(CursorHandle, OwnedHandleDoesNotTouchProvider) {
    FakeCursorProvider provider;
    {
        CursorHandle handle = CursorHandle::own(std::make_unique<WtCursor>());
        EXPECT_FALSE(handle.valid());
    }
    EXPECT_EQ(provider.releases, 0) << "owned 句柄自己 close，不经 provider ✓";
}

TEST_F(GraphSessionCursorProviderTest, UsesInjectedProviderForBothCursorKinds) {
    auto provider = std::make_shared<FakeCursorProvider>();
    auto* raw = provider.get();
    GraphSession session(conn_.make_session(), provider);

    {
        auto cursor = session.acquire_cursor("table:vertex");
        EXPECT_EQ(raw->acquires, 1);
        EXPECT_EQ(raw->last_table, "table:vertex");
        EXPECT_FALSE(cursor.valid());
    }
    EXPECT_EQ(raw->releases, 1);

    auto scan = session.acquire_scan_cursor("table:scan");
    EXPECT_EQ(raw->scans, 1);
    EXPECT_EQ(raw->last_table, "table:scan");
    scan.reset();
    EXPECT_EQ(raw->releases, 1) << "owned 扫描 cursor 不归还 provider ✓";
}

TEST_F(GraphSessionCursorProviderTest, CloseAndResetClearProvider) {
    auto provider = std::make_shared<FakeCursorProvider>();
    auto* raw = provider.get();
    GraphSession session(conn_.make_session(), provider);

    session.close();
    EXPECT_EQ(raw->clears, 1) << "会话收尾必须关闭缓存 cursor ✓";

    session.reset_for_reuse();
    EXPECT_EQ(raw->clears, 2) << "复用前清理缓存 cursor ✓";

    session.close();
    EXPECT_EQ(raw->clears, 3);
    EXPECT_TRUE(session.closed());
}

TEST_F(GraphSessionCursorProviderTest, DefaultProviderOpensRealCursorAndReportsFailure) {
    GraphSession session(conn_.make_session());
    auto cursor = session.acquire_cursor("table:missing");
    EXPECT_FALSE(cursor.valid()) << "缺失表 ⇒ 打开失败必须可见 ✓";
    EXPECT_NE(cursor.error(), 0) << "失败必须带错误码 ✓";
}
