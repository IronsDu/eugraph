#pragma once
/// 每图一份的 **schema 代数 pin 计数**（纯逻辑，可单测）。
///
/// 用途（设计 §9.3/§10.1）：DROP 一个索引后，不仅要等"读句柄归零"，还要等
/// **pin 早于删除时刻的事务**结束——它们可能已经计划好、稍后才去扫这个索引。
///
/// 语义：
///   * `pin(generation)` 在事务绑定 SchemaPin 时调用一次；`unpin` 在事务结束时调用一次；
///   * `minActiveGeneration()` 返回当前最老 pin（没有则 UINT64_MAX）；
///   * `waitNoPinsOlderThan(g, timeout)`：等到 minActiveGeneration >= g（没有 pin 也算）。

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>

namespace eugraph {

class SchemaGenerationTracker {
public:
    void pin(uint64_t generation) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            ++counts_[generation];
        }
        cv_.notify_all();
    }

    void unpin(uint64_t generation) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = counts_.find(generation);
            if (it == counts_.end())
                return;
            if (--it->second == 0)
                counts_.erase(it);
        }
        cv_.notify_all();
    }

    uint64_t minActiveGeneration() const {
        std::lock_guard<std::mutex> lock(mu_);
        return counts_.empty() ? UINT64_MAX : counts_.begin()->first;
    }

    size_t pinCount() const {
        std::lock_guard<std::mutex> lock(mu_);
        size_t total = 0;
        for (const auto& [generation, count] : counts_)
            total += count;
        return total;
    }

    bool waitNoPinsOlderThan(uint64_t generation, uint32_t timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        const auto done = [&] { return counts_.empty() || counts_.begin()->first >= generation; };
        if (done())
            return true;
        if (timeout_ms == 0)
            return false;
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), done);
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::map<uint64_t, size_t> counts_; ///< generation → active pin count
};

} // namespace eugraph
