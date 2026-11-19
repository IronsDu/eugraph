#pragma once
/// 索引构建/生命周期的**测试钩子**（设计 §2.1 of test-plan）。
///
/// 目的：并发/窗口类用例必须能控制时序，不能拿 sleep 当时钟。
/// 约定：
///   * 生产代码里**恒存在**，默认 no-op；只在测试里 `install()`；
///   * 未 install 时只有一次 atomic 检查（`installed()`），没有锁、没有 std::function 拷贝；
///   * handler 在钩子点被**同步调用**，测试可以在里面阻塞（用条件变量放行）。
///
/// 钩子点名（与测试计划对齐）：
///   `scan.after_read_props` / `scan.batch_committed` / `catchup.batch_applied` /
///   `finalize.gate_closed` / `finalize.before_public` / `admission.enter` / `commit.before_validate`

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <utility>

namespace eugraph::index_hooks {

using Handler = std::function<void(std::string_view point, uint64_t index_id)>;

inline std::atomic<bool>& installed() {
    static std::atomic<bool> flag{false};
    return flag;
}

inline std::mutex& handlerMutex() {
    static std::mutex mutex;
    return mutex;
}

inline Handler& handler() {
    static Handler h;
    return h;
}

/// 安装处理器（测试用）。传空 handler 等价于 clear()。
inline void install(Handler h) {
    {
        std::lock_guard<std::mutex> lock(handlerMutex());
        handler() = std::move(h);
    }
    installed().store(handler() != nullptr, std::memory_order_release);
}

inline void clear() {
    {
        std::lock_guard<std::mutex> lock(handlerMutex());
        handler() = nullptr;
    }
    installed().store(false, std::memory_order_release);
}

inline bool hasHandler() {
    return installed().load(std::memory_order_acquire);
}

/// 触发一个命名暂停点。未安装时只读一个 atomic。
inline void point(std::string_view name, uint64_t index_id) {
    if (!installed().load(std::memory_order_acquire))
        return;
    Handler copy;
    {
        std::lock_guard<std::mutex> lock(handlerMutex());
        copy = handler();
    }
    if (copy)
        copy(name, index_id);
}

} // namespace eugraph::index_hooks
