#include "storage/index/index_runtime_registry.hpp"

#include <folly/coro/Baton.h>
#include <folly/coro/Task.h>

#include <memory>
#include <mutex>

namespace eugraph {

bool IndexRuntime::closeAdmission() {
    std::lock_guard<std::mutex> gate_lock(gate_mu_);
    const bool was_open = admission_open_.exchange(false, std::memory_order_acq_rel);
    if (was_open) {
        // 新 epoch：关闸前没有等待者（闸门开着），后续等待者拿新 baton。
        admission_baton_ = std::make_shared<folly::coro::Baton>();
    }
    { std::lock_guard<std::mutex> lock(wait_mu_); }
    wait_cv_.notify_all();
    return was_open;
}

void IndexRuntime::reopenAdmission() {
    std::shared_ptr<folly::coro::Baton> wake;
    {
        std::lock_guard<std::mutex> gate_lock(gate_mu_);
        if (admission_open_.load(std::memory_order_acquire)) {
            // 已经开着：不要重复 post（Baton 需要 reset 才能复用）。
            return;
        }
        admission_open_.store(true, std::memory_order_release);
        wake = admission_baton_;
    }
    if (wake) {
        wake->post();
    }
    { std::lock_guard<std::mutex> lock(wait_mu_); }
    wait_cv_.notify_all();
}

folly::coro::Task<void> IndexRuntime::waitForAdmission() {
    while (!admission_open_.load(std::memory_order_acquire)) {
        std::shared_ptr<folly::coro::Baton> wait_on;
        {
            std::lock_guard<std::mutex> gate_lock(gate_mu_);
            if (admission_open_.load(std::memory_order_acquire)) {
                co_return;
            }
            wait_on = admission_baton_;
        }
        if (!wait_on)
            co_return;
        co_await *wait_on;
    }
}

} // namespace eugraph
