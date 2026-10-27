#include "storage/wt_table_ops.hpp"

#include "storage/data/graph_session.hpp"

#include <cstring>
#include <spdlog/spdlog.h>

#include "storage/storage_op_metrics.hpp"

namespace eugraph {

// ==================== WT Item Helpers ====================
//
// 所有原语都返回 **WT 错误码**（0 = 成功）而不是 bool ✓：
// 会话路径必须据此把失败粘到 `GraphSession` 上，从而禁止后续 commit ✓。

namespace {

using ScanCallback = std::function<bool(std::string_view, std::string_view)>;

void setItem(WT_CURSOR* cursor, std::string_view data) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = data.data();
    item.size = data.size();
    cursor->set_key(cursor, &item);
}

void setValueItem(WT_CURSOR* cursor, std::string_view data) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = data.data();
    item.size = data.size();
    cursor->set_value(cursor, &item);
}

int getValueFromCursor(WT_CURSOR* cursor, std::string& out) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    const int ret = cursor->get_value(cursor, &item);
    if (ret == 0)
        out.assign(static_cast<const char*>(item.data), item.size);
    return ret;
}

int getKeyFromCursor(WT_CURSOR* cursor, std::string& out) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    const int ret = cursor->get_key(cursor, &item);
    if (ret == 0)
        out.assign(static_cast<const char*>(item.data), item.size);
    return ret;
}

/// 在给定 cursor 上做一次 point put（调用方保证 cursor 有效 ✓）
int putWithCursor(WT_CURSOR* cursor, const std::string& table, std::string_view key, std::string_view value) {
    setItem(cursor, key);
    setValueItem(cursor, value);
    const int ret = cursor->insert(cursor);
    if (ret != 0)
        spdlog::error("table_put failed on {}: error {}", table, ret);
    return ret;
}

/// 返回 0 / WT_NOTFOUND / 其他错误码 ✓
int getWithCursor(WT_CURSOR* cursor, std::string_view key, std::string& out) {
    setItem(cursor, key);
    const int ret = (recordWtSearch(), cursor->search(cursor));
    if (ret != 0)
        return ret;
    return getValueFromCursor(cursor, out);
}

/// WT_NOTFOUND 视为删除成功 ✓（幂等删除语义）
int delWithCursor(WT_CURSOR* cursor, const std::string& table, std::string_view key) {
    setItem(cursor, key);
    const int ret = cursor->remove(cursor);
    if (ret != 0 && ret != WT_NOTFOUND) {
        spdlog::error("table_del failed on {}: error {}", table, ret);
        return ret;
    }
    return 0;
}

/// 区间/前缀迭代的**唯一原语** ✓（tableScan 与 index range scan 共用）
/// 返回 0 = 正常结束（含 WT_NOTFOUND）；其他 = 错误码 ✓
int scanRangeWithCursor(WT_CURSOR* c, std::string_view start_key, bool exclusive_start, std::string_view end_key,
                        const std::function<std::string_view(std::string_view)>& end_key_view,
                        const ScanCallback& callback) {
    if (c == nullptr)
        return EINVAL;
    int ret = 0;
    if (!start_key.empty()) {
        setItem(c, start_key);
        int cmp = 0;
        ret = (recordWtSearchNear(), c->search_near(c, &cmp));
        if (ret == WT_NOTFOUND)
            return 0;
        if (ret != 0)
            return ret;
        if (exclusive_start ? cmp <= 0 : cmp < 0) {
            ret = c->next(c);
            if (ret == WT_NOTFOUND)
                return 0;
            if (ret != 0)
                return ret;
        }
    } else {
        ret = c->reset(c);
        if (ret != 0)
            return ret;
        ret = c->next(c);
        if (ret == WT_NOTFOUND)
            return 0;
        if (ret != 0)
            return ret;
    }

    while (true) {
        std::string key;
        if ((ret = getKeyFromCursor(c, key)) != 0)
            return ret;
        if (!end_key.empty()) {
            const std::string_view cmp_key = end_key_view ? end_key_view(key) : std::string_view(key);
            if (cmp_key >= end_key)
                return 0;
        }
        std::string value;
        if ((ret = getValueFromCursor(c, value)) != 0)
            return ret;
        if (!callback(key, value))
            return 0;
        ret = c->next(c);
        if (ret == WT_NOTFOUND)
            return 0;
        if (ret != 0)
            return ret;
    }
}

/// 前缀扫描（table_scan）建立在同一原语之上 ✓
int scanWithCursor(WT_CURSOR* c, std::string_view prefix, const ScanCallback& callback) {
    if (prefix.empty())
        return scanRangeWithCursor(c, {}, false, {}, {}, callback);
    return scanRangeWithCursor(c, prefix, false, {}, {}, [&](std::string_view key, std::string_view value) {
        return key.starts_with(prefix) && callback(key, value);
    });
}

} // anonymous namespace

// ==================== Table KV Operations (explicit WT session) ====================

bool wt::table_put(WT_SESSION* session, const std::string& table, std::string_view key, std::string_view value) {
    if (session == nullptr) {
        spdlog::error("table_put: null session");
        return false;
    }
    WtCursor cursor = open_cursor(session, table);
    if (!cursor)
        return false;
    return putWithCursor(cursor.get(), table, key, value) == 0;
}

std::optional<std::string> wt::table_get(WT_SESSION* session, const std::string& table, std::string_view key) {
    if (session == nullptr) {
        spdlog::error("table_get: null session");
        return std::nullopt;
    }
    WtCursor cursor = open_cursor(session, table);
    if (!cursor)
        return std::nullopt;
    std::string value;
    if (getWithCursor(cursor.get(), key, value) != 0)
        return std::nullopt;
    return value;
}

bool wt::table_del(WT_SESSION* session, const std::string& table, std::string_view key) {
    if (session == nullptr) {
        spdlog::error("table_del: null session");
        return false;
    }
    WtCursor cursor = open_cursor(session, table);
    if (!cursor)
        return false;
    return delWithCursor(cursor.get(), table, key) == 0;
}

void wt::table_scan(WT_SESSION* session, const std::string& table, std::string_view prefix,
                    const std::function<bool(std::string_view, std::string_view)>& callback) {
    if (session == nullptr) {
        spdlog::error("table_scan: null session");
        return;
    }
    WtCursor cursor = open_cursor(session, table);
    if (!cursor)
        return;
    const int ret = scanWithCursor(cursor.get(), prefix, callback);
    if (ret != 0)
        spdlog::error("table_scan failed on {}: error {}", table, ret);
}

// ==================== Session-aware KV Operations ====================
//
// 失败**必须粘到会话** ✓：即使上层忽略了返回值，`GraphSession::commit()` 也会拒绝提交 ✓
// （否则「部分写入 + commit 成功」会静默丢数据 ✗）。

bool wt::table_put(GraphSession& session, const std::string& table, std::string_view key, std::string_view value) {
    CursorHandle cursor = session.acquire_cursor(table);
    if (!cursor) {
        session.fail_from_wt(cursor.error(), "table_put: cannot open cursor on " + table);
        return false;
    }
    const int ret = putWithCursor(cursor.get(), table, key, value);
    if (ret != 0) {
        session.fail_from_wt(ret, "table_put failed on " + table);
        return false;
    }
    return true;
}

std::optional<std::string> wt::table_get(GraphSession& session, const std::string& table, std::string_view key) {
    CursorHandle cursor = session.acquire_cursor(table);
    if (!cursor) {
        session.fail_from_wt(cursor.error(), "table_get: cannot open cursor on " + table);
        return std::nullopt;
    }
    std::string value;
    const int ret = getWithCursor(cursor.get(), key, value);
    if (ret == WT_NOTFOUND)
        return std::nullopt;
    if (ret != 0) {
        session.fail_from_wt(ret, "table_get failed on " + table);
        return std::nullopt;
    }
    return value;
}

bool wt::table_del(GraphSession& session, const std::string& table, std::string_view key) {
    CursorHandle cursor = session.acquire_cursor(table);
    if (!cursor) {
        session.fail_from_wt(cursor.error(), "table_del: cannot open cursor on " + table);
        return false;
    }
    const int ret = delWithCursor(cursor.get(), table, key);
    if (ret != 0) {
        session.fail_from_wt(ret, "table_del failed on " + table);
        return false;
    }
    return true;
}

void wt::table_scan(GraphSession& session, const std::string& table, std::string_view prefix,
                    const std::function<bool(std::string_view, std::string_view)>& callback) {
    // 扫描用独立 cursor（同表嵌套扫描/边扫边删是真实路径 ✓，共享缓存 cursor 会互相破坏 ✗）
    CursorHandle cursor = session.acquire_scan_cursor(table);
    if (!cursor) {
        session.fail_from_wt(cursor.error(), "table_scan: cannot open cursor on " + table);
        return;
    }
    const int ret = scanWithCursor(cursor.get(), prefix, callback);
    if (ret != 0)
        session.fail_from_wt(ret, "table_scan failed on " + table);
}

int wt::scan_range(WT_CURSOR* cursor, std::string_view start_key, bool exclusive_start, std::string_view end_key,
                   const std::function<std::string_view(std::string_view)>& end_key_view,
                   const std::function<bool(std::string_view, std::string_view)>& callback) {
    return scanRangeWithCursor(cursor, start_key, exclusive_start, end_key, end_key_view, callback);
}

} // namespace eugraph
