#pragma once
/// 每图一个的索引构建服务：把「调度器」「相位机」「发布状态」「关图编排」合到一处。
///
/// 职责（对应设计 §8 / §13-P1）：
///  * 拥有**每图一个** `IndexBuildScheduler`（并发度可配，默认 2；1 = 串行）；
///  * `submit()` 把一个构建任务交给相位机跑（**在后台线程上**，不阻塞 DDL 调用方）；
///  * 相位机判定 `PUBLIC`/`ERROR`/`CANCELLED` 后，由本服务调用**注入的发布回调**落元数据
///    （保持两库顺序：先数据、后状态，§7.3）；
///  * `cancel(index_id)` 供 `DROP` 使用；`shutdown()` 供**关图**使用：先取消再 drain
///    （顺序错会 use-after-close，设计 H17）。
///
/// 可测试性：`BuildRunner`（真正干活的回调）与 `Publisher`（落状态）都是注入的 ⇒ 单测用假实现即可，
/// 不需要 WiredTiger 或服务端；**无裸指针**（`std::function` / `shared_ptr`）。
#include "storage/index/index_build_scheduler.hpp"
#include "storage/index/index_build_task.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {

class IndexBuildService {
public:
    /// 真正执行构建的回调：内部通常用相位机跑完整流程并返回结果。
    using BuildRunner = std::function<IndexBuildResult(uint64_t index_id)>;
    /// 发布最终状态（落 meta）：`PUBLIC` 只在 result.outcome==PUBLIC 时调用。
    using Publisher = std::function<void(uint64_t index_id, IndexBuildOutcome outcome, const std::string& error)>;

    IndexBuildService(BuildRunner runner, Publisher publisher, IndexBuildScheduler::Options opts = {})
        : runner_(std::move(runner)), publisher_(std::move(publisher)), scheduler_(opts) {}
    // 说明：**不注入自定义运行器**——用调度器自带的线程运行器（它自己持有并 join 线程）。
    // 曾用自定义运行器另起线程，结果没人 join ⇒ std::thread 析构会 std::terminate。

    ~IndexBuildService() {
        shutdown();
    }

    IndexBuildService(const IndexBuildService&) = delete;
    IndexBuildService& operator=(const IndexBuildService&) = delete;

    /// 提交一个索引的构建任务。返回 false = 该索引已在排队或运行中（幂等）。
    bool submit(uint64_t index_id, std::string name) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (shutting_down_)
                return false; // 关图后不再接受新任务
        }
        return scheduler_.submit(index_id, std::move(name), [this, index_id] { execute(index_id); });
    }

    /// `DROP` 命中：取消该索引自己的任务（排队中丢弃；运行中由相位机在阶段边界察觉）。
    bool cancel(uint64_t index_id) {
        return scheduler_.cancel(index_id);
    }

    bool isCancelled(uint64_t index_id) const {
        return scheduler_.isCancelled(index_id);
    }

    size_t runningCount() const {
        return scheduler_.runningCount();
    }
    size_t queuedCount() const {
        return scheduler_.queuedCount();
    }

    /// 该索引当前是否正由本服务构建（排队或运行中）
    bool isBuilding(uint64_t index_id) const {
        for (const auto& e : scheduler_.snapshot()) {
            if (e.index_id == index_id)
                return true;
        }
        return false;
    }

    /// 关图/停机：先请求取消全部任务，再等它们结束（**顺序不可颠倒**，见 H17）。
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (shutting_down_)
                return;
            shutting_down_ = true;
        }
        // 全部取消（**排队中的也必须取消**，否则 drain 期间它们会被启动并真正跑起来）
        for (const auto& e : scheduler_.snapshot())
            scheduler_.cancel(e.index_id);
        scheduler_.drain(); // 等所有任务结束并回收线程
    }

private:
    void execute(uint64_t index_id) {
        IndexBuildResult result;
        try {
            result =
                runner_ ? runner_(index_id) : IndexBuildResult{IndexBuildOutcome::ERROR, "no build runner installed"};
        } catch (const std::exception& e) {
            result = IndexBuildResult{IndexBuildOutcome::ERROR, std::string("build threw: ") + e.what()};
        } catch (...) {
            result = IndexBuildResult{IndexBuildOutcome::ERROR, "build threw (unknown)"};
        }
        // 三种结果都发布：CANCELLED 由调用方（DROP/关图）负责清理，PUBLIC/ERROR 落元数据
        if (publisher_)
            publisher_(index_id, result.outcome, result.error);
    }

    BuildRunner runner_;
    Publisher publisher_;
    mutable std::mutex mu_;
    bool shutting_down_ = false;
    IndexBuildScheduler scheduler_;
};

} // namespace eugraph
