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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
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
    using Publisher = std::function<void(uint64_t index_id, const std::string& name, IndexBuildOutcome outcome,
                                         const std::string& error)>;

    /// `scheduler_runner` 可注入（默认空 ⇒ 调度器自带**线程**运行器）。
    /// **测试注入"同步运行器"** ⇒ 任务在提交点直接执行完 ⇒ 断言无需等待、**完全不依赖时间**。
    IndexBuildService(BuildRunner runner, Publisher publisher, IndexBuildScheduler::Options opts = {},
                      IndexBuildScheduler::Runner scheduler_runner = {})
        : runner_(std::move(runner)), publisher_(std::move(publisher)), scheduler_(opts, std::move(scheduler_runner)) {}
    // 说明：默认用调度器自带的**线程**运行器（它自己持有并 join 线程）——早前用自定义运行器另起线程
    // 却没人 join，导致 std::thread 析构 terminate。此处仅在**测试**注入同步运行器（无线程、无等待）。

    ~IndexBuildService() {
        shutdown();
    }

    IndexBuildService(const IndexBuildService&) = delete;
    IndexBuildService& operator=(const IndexBuildService&) = delete;

    /// 每个索引自带的构建闭包（捕获 stmt/表名/解析结果等）。
    /// 有了它，服务不必知道"怎么建索引"，只负责调度/取消/发布生命周期。
    using BuildJob = std::function<IndexBuildResult()>;

    /// 提交一个索引的构建任务（按任务版本）。返回 false = 已在排队或运行中（幂等）。
    bool submit(uint64_t index_id, std::string name, BuildJob job) {
        if (!job)
            return false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (shutting_down_)
                return false;
        }
        // 名字要留给发布回调（落状态需要索引名）⇒ 复制一份进任务闭包
        std::string job_name = name;
        return scheduler_.submit(index_id, std::move(name), [this, index_id, job_name, job = std::move(job)] {
            execute_job(index_id, job_name, job);
        });
    }

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

    /// 取消并**等待任务真正退出**。`DROP` 删索引表之前**必须**调用：
    /// 只置取消位就删表 ⇒ 回填仍持有该表 ⇒ 删表失败（"Device or resource busy"），
    /// 且"定义已删、表仍在"的坏状态下继续 ⇒ **实测 SIGSEGV**（设计 H13/H17 同类竞态）。
    bool cancelAndWait(uint64_t index_id, std::chrono::milliseconds timeout = std::chrono::milliseconds(10000)) {
        scheduler_.cancel(index_id);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!isBuilding(index_id))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return !isBuilding(index_id);
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

    // ==================== 孤儿表日志（跨进程回收，设计 §20.22 / §20.18 B）====================
    /// 设定孤儿表日志路径（每图一份，随图目录）。为空 ⇒ 只做进程内登记。
    void setOrphanLogPath(std::string path) {
        orphan_log_path_ = std::move(path);
    }

    /// 记录一张删不掉的索引表（去重）。**跨进程回收**：下次打开该图时重试删除。
    void recordOrphan(const std::string& table) {
        if (table.empty())
            return;
        std::lock_guard<std::mutex> lock(orphan_mu_);
        if (orphan_log_path_.empty())
            return;
        auto existing = readOrphansLocked();
        if (std::find(existing.begin(), existing.end(), table) != existing.end())
            return;
        existing.push_back(table);
        writeOrphansLocked(existing);
    }

    /// 读取待回收的孤儿表（打开图时用），并清空日志（失败者由调用方重新登记）
    std::vector<std::string> takeOrphans() {
        std::lock_guard<std::mutex> lock(orphan_mu_);
        auto existing = readOrphansLocked();
        writeOrphansLocked({});
        return existing;
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
    /// 执行"按任务"版本：任务自带闭包（含索引名），异常同样经发布回调收口。
    void execute_job(uint64_t index_id, const std::string& name, const BuildJob& job) {
        IndexBuildResult result;
        try {
            result = job();
        } catch (const std::exception& e) {
            result = IndexBuildResult{IndexBuildOutcome::ERROR, std::string("build threw: ") + e.what()};
        } catch (...) {
            result = IndexBuildResult{IndexBuildOutcome::ERROR, "build threw (unknown)"};
        }
        if (publisher_)
            publisher_(index_id, name, result.outcome, result.error);
    }

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
            publisher_(index_id, std::string{}, result.outcome, result.error); // ctor-runner 路径无名字
    }

    BuildRunner runner_;
    Publisher publisher_;
    mutable std::mutex mu_;
    std::string orphan_log_path_;
    mutable std::mutex orphan_mu_;
    std::vector<std::string> readOrphansLocked() const {
        std::vector<std::string> out;
        if (orphan_log_path_.empty())
            return out;
        std::ifstream in(orphan_log_path_);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty())
                out.push_back(line);
        }
        return out;
    }
    void writeOrphansLocked(const std::vector<std::string>& tables) const {
        if (orphan_log_path_.empty())
            return;
        std::ofstream out(orphan_log_path_, std::ios::trunc);
        for (const auto& t : tables)
            out << t << '\n';
    }

    bool shutting_down_ = false;
    IndexBuildScheduler scheduler_;
};

} // namespace eugraph
