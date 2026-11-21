#include "graph/graph_manager.hpp"

#include "storage/index/index_runtime_factory.hpp"

#include <folly/coro/BlockingWait.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <thread>

namespace eugraph {

GraphManager::~GraphManager() {
    if (running_.load()) {
        shutdown();
    }
}

bool GraphManager::init(const std::string& data_dir, int io_threads, int compute_threads, int checkpoint_interval_sec,
                        const std::string& data_wt_config) {
    data_dir_ = data_dir;
    io_threads_ = io_threads;
    compute_threads_ = compute_threads;
    checkpoint_interval_sec_ = checkpoint_interval_sec;
    data_wt_config_ = data_wt_config;

    auto t0 = std::chrono::steady_clock::now();

    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    if (ec) {
        spdlog::error("Failed to create data directory: {}", ec.message());
        return false;
    }

    spdlog::info("Opening catalog...");
    if (!catalog_.open(data_dir_ + "/catalog")) {
        spdlog::error("Failed to open catalog store");
        return false;
    }

    io_scheduler_ = std::make_shared<IoScheduler>(io_threads_);
    compute_pool_ = std::make_shared<folly::CPUThreadPoolExecutor>(
        static_cast<size_t>(compute_threads_), std::make_shared<folly::NamedThreadFactory>("eugraph-compute"));
    {
        const char* mode_name = "same";
        switch (io_scheduler_->mode()) {
        case IoScheduler::DispatchMode::Pool:
            mode_name = "pool";
            break;
        case IoScheduler::DispatchMode::Inline:
            mode_name = "inline";
            break;
        case IoScheduler::DispatchMode::SameExecutor:
            mode_name = "same";
            break;
        }
        spdlog::info("Storage dispatch: {} (compute threads {}, storage IO threads {}; session control stays on the "
                     "IO pool)",
                     mode_name, compute_threads_, io_threads_);
    }

    auto entries = catalog_.listGraphs();
    spdlog::info("Found {} graph(s) in catalog, opening...", entries.size());

    for (auto& entry : entries) {
        auto t_graph = std::chrono::steady_clock::now();
        auto instance = openGraphInstance(entry.graph_id, entry.name);
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_graph).count();
        if (instance) {
            graphs_[entry.name] = std::move(instance);
            spdlog::info("Loaded graph '{}' (id={}) in {}ms", entry.name, entry.graph_id, ms);
        } else {
            // A MISSING directory means a stale catalog entry (the data is gone) -> drop the entry.
            // A directory that EXISTS but cannot be opened (incompatible/truncated format) must keep
            // its entry: dropping it would silently remove the graph from the catalog and make the
            // data unreachable with no way back.
            std::error_code ec;
            const std::string graph_dir = data_dir_ + "/graph_" + std::to_string(entry.graph_id);
            if (!std::filesystem::exists(graph_dir, ec)) {
                spdlog::warn("Graph '{}' (id={}) has no directory ({}); dropping its stale catalog entry", entry.name,
                             entry.graph_id, graph_dir);
                catalog_.dropGraph(entry.name);
            } else {
                spdlog::error("Graph '{}' (id={}) could not be opened in {}ms; keeping its catalog entry (see the "
                              "error above). Fix or re-import this graph's data, then restart.",
                              entry.name, entry.graph_id, ms);
            }
        }
    }

    if (!catalog_.getGraph(kDefaultGraphName).has_value()) {
        spdlog::info("Creating default graph...");
        auto entry = catalog_.createGraph(kDefaultGraphName);
        if (!entry.has_value()) {
            spdlog::error("Failed to create default graph");
            return false;
        }
        auto instance = openGraphInstance(entry->graph_id, kDefaultGraphName);
        if (!instance) {
            spdlog::error("Failed to open default graph instance");
            return false;
        }
        graphs_[kDefaultGraphName] = std::move(instance);
        spdlog::info("Created default graph (id={})", entry->graph_id);
    }

    running_.store(true);
    checkpoint_thread_ = std::thread(&GraphManager::checkpointLoop, this);

    auto total_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    spdlog::info("GraphManager initialized in {}ms ({} graphs loaded, checkpoint interval {}s)", total_ms,
                 graphs_.size(), checkpoint_interval_sec_);

    return true;
}

void GraphManager::shutdown() {
    if (!running_.exchange(false))
        return;

    {
        std::lock_guard<std::mutex> lk(checkpoint_mu_);
        checkpoint_cv_.notify_one();
    }
    if (checkpoint_thread_.joinable())
        checkpoint_thread_.join();

    // Extract instances first, then close without holding the lock
    std::vector<std::unique_ptr<GraphInstance>> instances;
    {
        std::unique_lock lock(mu_);
        instances.reserve(graphs_.size());
        for (auto& [name, inst] : graphs_) {
            instances.push_back(std::move(inst));
        }
        graphs_.clear();
    }

    // 关闭前先关闸并等在飞使用者归零：否则会与仍在执行的协程并发使用同一 WT 连接 ⇒ WT_PANIC。
    // 超时仍不归零 ⇒ **故意泄漏**该实例（进程即将退出；泄漏远好于 UAF/WT_PANIC ✓）。
    for (auto& inst : instances)
        if (inst && inst->index_builds)
            inst->index_builds->shutdown();
    for (auto& inst : instances)
        if (inst && inst->usage)
            inst->usage->close();
    for (auto& inst : instances) {
        if (!inst || !inst->usage)
            continue;
        if (inst->usage->waitDrained(/*timeout_ms=*/10000))
            continue;
        spdlog::error("shutdown: 图 '{}' 仍有 {} 个在飞使用者，超时 ⇒ 泄漏该实例以避免 UAF/WT_PANIC", inst->name,
                      inst->usage->inflight());
        (void)inst.release(); // 有序泄漏：连接/会话/池都保持有效，直到进程退出
    }

    // 图实例析构顺序保证「会话池 → store → 连接」：session_pool 声明在最后 ⇒ 最先销毁 ✓
    instances.clear();
    catalog_.close();
}

GraphEntry GraphManager::createGraph(const std::string& name) {
    std::unique_lock lock(mu_);

    auto existing = catalog_.getGraph(name);
    if (existing.has_value())
        return *existing;

    auto entry = catalog_.createGraph(name);
    if (!entry.has_value())
        throw std::runtime_error("Failed to create graph: " + name);

    auto instance = openGraphInstance(entry->graph_id, name);
    if (!instance) {
        catalog_.dropGraph(name);
        throw std::runtime_error("Failed to open graph instance: " + name);
    }

    graphs_[name] = std::move(instance);
    return *entry;
}

bool GraphManager::dropGraph(const std::string& name) {
    if (name == kDefaultGraphName)
        throw std::runtime_error("Cannot drop the default graph");

    // Move the instance out of the map under the lock, then release the lock
    // before performing blocking close operations. This prevents the IO pool
    // threads from being blocked while holding the exclusive lock, which would
    // deadlock handler coroutines that need a shared_lock (e.g. resolveGraph
    // during executeCypher).
    //
    // Moving out of the map (rather than just extracting a raw pointer) also
    // prevents a race with checkpointAll(): since the entry is removed from
    // graphs_ immediately, the checkpoint thread can never see a graph that
    // is in the process of being closed.
    std::unique_ptr<GraphInstance> inst;
    uint32_t graph_id = 0;
    {
        std::unique_lock lock(mu_);
        auto it = graphs_.find(name);
        if (it == graphs_.end())
            throw std::runtime_error("Graph not found: " + name);
        inst = std::move(it->second);
        graph_id = inst->graph_id;
        graphs_.erase(it);
        catalog_.dropGraph(name);
    }
    // **先关闸再等在飞使用者归零**：否则并发使用同一 WT 连接 ⇒ `__conn_close` 失败 ⇒ WT_PANIC
    // ⇒ 此后进程内写入静默丢失、后续 CREATE DATABASE 打不开图实例（实测，见
    // docs/tests/bolt-driver-integration-notes.md）。
    // 构建任务不持有 usage 租约 ⇒ 必须先取消并 join，再关连接（H17）。
    if (inst->index_builds)
        inst->index_builds->shutdown();
    inst->usage->close();
    if (!inst->usage->waitDrained(/*timeout_ms=*/10000)) {
        // 仍有执行流在用这个图的会话/连接 ⇒ **绝不能**关闭连接（UAF ⇒ WT_PANIC ⇒ 全局静默丢写）。
        // 安全失败模式：故意泄漏实例（连接/会话池保持有效，执行流释放完租约后自然回收），
        // 图目录也不删除（避免删掉仍被打开的数据）。catalog 条目已在锁内移除；
        // 该目录成为孤儿数据，需要人工处理 —— 这仍然好过 UAF。
        spdlog::error("dropGraph '{}': 仍有 {} 个在飞使用者与 {} 个借出会话，超时 ⇒ 泄漏实例以避免 UAF/WT_PANIC", name,
                      inst->usage->inflight(), inst->session_pool ? inst->session_pool->outstanding() : 0);
        (void)inst.release();
        return false;
    }
    // lock released — safe to block on I/O; inst owns the GraphInstance
    // 连接关闭前先清池 ✓；随后 inst 析构（session_pool → store → 连接 ✓）
    // 双保险：usage 归零但仍有借出会话（说明租约生命周期与 usage 顺序出错）⇒ 同样不能关连接。
    if (inst->session_pool && inst->session_pool->outstanding() != 0) {
        spdlog::error("dropGraph '{}': usage 已归零但仍有 {} 个借出会话 ⇒ 泄漏实例以避免 UAF", name,
                      inst->session_pool->outstanding());
        (void)inst.release();
        return false;
    }
    if (inst->session_pool)
        inst->session_pool->clear();
    inst.reset();

    std::string graph_dir = data_dir_ + "/graph_" + std::to_string(graph_id);
    std::error_code ec;
    std::filesystem::remove_all(graph_dir, ec);
    if (ec)
        spdlog::warn("Failed to clean graph directory {}: {}", graph_dir, ec.message());

    return true;
}

std::vector<GraphEntry> GraphManager::listGraphs() {
    std::shared_lock lock(mu_);
    return catalog_.listGraphs();
}

std::optional<GraphLease> GraphManager::acquireGraph(const std::string& name) {
    // 关键：tryEnter 必须在 shared_lock 内完成。否则 get 返回后、tryEnter 前，
    // dropGraph 可以移出并销毁实例 ⇒ UAF。
    std::shared_lock lock(mu_);
    auto it = graphs_.find(name);
    if (it == graphs_.end())
        return std::nullopt;
    auto guard = it->second->usage->tryEnter();
    if (!guard)
        return std::nullopt;
    return GraphLease{it->second.get(), std::move(guard)};
}

std::unique_ptr<GraphInstance> GraphManager::openGraphInstance(uint32_t graph_id, const std::string& name) {
    // Opening a store can throw: the metadata decoders reject a truncated or older on-disk format
    // ("MetadataCodec: unexpected end of data decoding ..."). Without this catch the exception
    // escaped main() and reached std::terminate -> SIGABRT, so one unreadable graph took the whole
    // server down with no actionable message. A failed graph now reports itself and is skipped.
    try {
        return openGraphInstanceUnchecked(graph_id, name);
    } catch (const std::exception& e) {
        spdlog::error("Failed to open graph '{}' (id={}) under {}/graph_{}: {}", name, graph_id, data_dir_, graph_id,
                      e.what());
        spdlog::error("The graph directory exists but its data could not be read (incompatible or truncated on-disk "
                      "format). Re-import the data for this graph; other graphs are unaffected.");
        return nullptr;
    }
}

std::unique_ptr<GraphInstance> GraphManager::openGraphInstanceUnchecked(uint32_t graph_id, const std::string& name) {
    std::string graph_dir = data_dir_ + "/graph_" + std::to_string(graph_id);
    std::string data_dir = graph_dir + "/data";
    std::string meta_dir = graph_dir + "/meta";

    auto instance = std::make_unique<GraphInstance>();
    instance->graph_id = graph_id;
    instance->name = name;

    // 连接 → WtConnectionStore（data / meta 各一）✓；store 只 bind，不拥有连接 ✓
    if (!instance->data_conn.open(data_dir, data_wt_config_)) {
        spdlog::error("Failed to open data connection for graph '{}' at {}", name, data_dir);
        return nullptr;
    }
    if (!instance->meta_conn.open(meta_dir)) {
        spdlog::error("Failed to open meta connection for graph '{}' at {}", name, meta_dir);
        return nullptr;
    }

    instance->sync_data = std::make_unique<SyncGraphDataStore>();
    instance->sync_data->bind_connection(instance->data_conn);
    if (!instance->sync_data->ensure_tables()) {
        spdlog::error("Failed to ensure data tables for graph '{}' at {}", name, data_dir);
        return nullptr;
    }

    instance->sync_meta = std::make_unique<SyncGraphMetaStore>();
    instance->sync_meta->bind_connection(instance->meta_conn);
    if (!instance->sync_meta->ensure_tables()) {
        spdlog::error("Failed to ensure meta tables for graph '{}' at {}", name, meta_dir);
        return nullptr;
    }

    // 共享 async 门面只用于 for_session()；数据会话只作为构造锚点。
    instance->async_session = std::make_unique<GraphSession>(instance->data_conn.make_session());
    instance->async_data =
        std::make_unique<AsyncGraphDataStore>(*instance->sync_data, *io_scheduler_, *instance->async_session);

    instance->async_meta = std::make_unique<AsyncGraphMetaStore>();
    auto opened = folly::coro::blockingWait(instance->async_meta->open(*instance->sync_meta, *io_scheduler_));
    if (!opened) {
        spdlog::error("Failed to initialize async meta store for graph '{}'", name);
        return nullptr;
    }

    // Planning heuristic: count vertices per label once per graph open. The
    // deferred destination-label filter uses these counts to decide whether a
    // full label_fwd scan or a per-vertex label read is cheaper. Counts may go
    // stale under writes; they are only a heuristic.
    {
        auto all_labels = folly::coro::blockingWait(instance->async_meta->listLabels());
        const auto t0 = std::chrono::steady_clock::now();
        uint64_t total = 0;
        for (const auto& label : all_labels) {
            uint64_t count = 0;
            instance->sync_data->scanVerticesByLabel(*instance->async_session, label.id, [&count](VertexId) {
                ++count;
                return true;
            });
            instance->async_meta->setLabelVertexCount(label.id, count);
            total += count;
        }
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        spdlog::info("Label vertex counts computed for graph '{}' ({} labels, {} vertices, {} ms)", name,
                     all_labels.size(), total, ms);
    }

    // Ensure __anon__ label exists for unlabeled node properties
    {
        auto anon_def = folly::coro::blockingWait(instance->async_meta->getLabelDef(std::string(kAnonLabelName)));
        if (!anon_def.has_value()) {
            auto anon_id =
                folly::coro::blockingWait(instance->async_meta->createLabel(std::string(kAnonLabelName), {}));
            if (anon_id == INVALID_LABEL_ID) {
                spdlog::error("Failed to create __anon__ label for graph '{}'", name);
                return nullptr;
            }
            folly::coro::blockingWait(instance->async_data->createLabel(anon_id));
            spdlog::info("Created __anon__ label (id={}) for graph '{}'", anon_id, name);
        }
    }

    // ==================== v2 索引运行时：启动修复与恢复 ====================
    // 1) 遗留缺陷修复：旧 createEdgeIndex 没分配 index_id（持久化为 0）。
    //    0 既不能注册也不能路由，这里一次性补 id 并落盘（幂等）。
    try {
        const auto repaired = folly::coro::blockingWait(instance->async_meta->repairMissingIndexIds());
        if (repaired > 0) {
            spdlog::warn("[index] graph '{}': repaired {} legacy index definition(s) with index_id==0", name, repaired);
        }
    } catch (const std::exception& e) {
        // 修复失败不阻塞打开：内存里已补 id，下次打开会再试。
        spdlog::error("[index] graph '{}': failed to repair legacy index ids: {}", name, e.what());
    }

    // 2) 重启恢复：WRITE_ONLY（构建中）意味着构建随进程死亡；本期不做续建（无扫描水位），
    //    一律置 ERROR 并保留定义；半成品表登记为 orphan。
    try {
        const auto recovered = folly::coro::blockingWait(instance->async_meta->recoverUnfinishedIndexes());
        if (!recovered.empty()) {
            spdlog::warn("[index] graph '{}': {} unfinished index build(s) marked ERROR (no crash resume)", name,
                         recovered.size());
        }
        // 3) 清理遗留表：启动时没有旧进程的会话占用，通常必成；失败则保留记录等下次启动。
        const auto records = folly::coro::blockingWait(instance->async_meta->listCleanupRecords());
        for (const auto& record : records) {
            bool all_dropped = true;
            for (const auto& table : record.tables) {
                if (!instance->sync_data->dropIndex(table)) {
                    all_dropped = false;
                    break;
                }
            }
            if (all_dropped) {
                folly::coro::blockingWait(instance->async_meta->removeCleanupRecord(record.kind, record.index_id));
                spdlog::info("[index] graph '{}': cleanup '{}' index {} done ({} tables)", name, record.kind,
                             record.index_id, record.tables.size());
            } else {
                spdlog::warn("[index] graph '{}': cleanup '{}' index {} still busy; retry on next open", name,
                             record.kind, record.index_id);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("[index] graph '{}': failed to recover/cleanup indexes: {}", name, e.what());
    }

    // 3) 运行时注册表：从持久化定义播种（PUBLIC 可被规划器选中；ERROR 只用于观测）。
    //    注册表先于 executor 声明 ⇒ executor 先销毁。
    instance->index_registry = std::make_shared<IndexRuntimeRegistry>();
    instance->schema_generations = std::make_shared<SchemaGenerationTracker>();
    {
        const GraphSchema schema_now = instance->async_meta->schemaSnapshot();
        size_t seeded = 0;
        for (const auto& [lid, label] : schema_now.labels) {
            for (const auto& idx : label.indexes) {
                if (idx.index_id == 0)
                    continue;
                if (instance->index_registry->createOrUpdate(makeIndexRuntimeDescriptor(idx, lid, false)))
                    ++seeded;
            }
        }
        for (const auto& [elid, elabel] : schema_now.edge_labels) {
            for (const auto& idx : elabel.indexes) {
                if (idx.index_id == 0)
                    continue;
                if (instance->index_registry->createOrUpdate(makeIndexRuntimeDescriptor(idx, elid, true)))
                    ++seeded;
            }
        }
        spdlog::info("[index] graph '{}': {} index runtime(s) registered", name, seeded);
    }
    // 所有语句级 AsyncGraphDataStore（for_session）都会继承这个指针；
    // 批量装载等 sync 路径用 sync_data 上的端口。
    instance->async_data->setIndexRegistry(instance->index_registry.get());
    instance->sync_data->setIndexRegistry(instance->index_registry.get());
    instance->stats_store = std::make_shared<GraphStatsStore>();
    instance->sync_data->setStatsStore(instance->stats_store.get());
    // 统计 journal 是数据 DB 的一部分：先恢复 snapshot，再重放其后的 journal。
    GraphStatsSnapshot stats_snapshot;
    if (instance->sync_data->loadStatsSnapshot(stats_snapshot)) {
        instance->stats_store->restore(stats_snapshot);
        instance->sync_data->replayStatsJournal(*instance->stats_store, stats_snapshot.applied_journal_seq);
    } else {
        instance->sync_data->replayStatsJournal(*instance->stats_store);
    }

    // 会话池：工厂从**连接所有者**取 session ✓（WT 不外泄 ✓）
    // 注：池成员声明在 GraphInstance **最后** ⇒ 最先销毁 ✓（session 在连接关闭前 close ✓，§3.2.1）
    instance->session_pool =
        std::make_shared<SessionPool>(32, [conn = &instance->data_conn]() { return conn->make_session(); });

    compute::QueryExecutor::Config executor_config;
    executor_config.compute_threads = compute_threads_;
    executor_config.compute_pool = compute_pool_;
    instance->executor = std::make_unique<compute::QueryExecutor>(*instance->async_data, *instance->async_meta,
                                                                  *instance->session_pool, executor_config);
    instance->executor->setIndexRegistry(instance->index_registry.get());
    instance->executor->setSchemaGenerationTracker(instance->schema_generations);
    instance->executor->setUsageGate(instance->usage);
    // 索引构建服务：CREATE INDEX 改成"投递即返回"，后台线程通过 executor 跑完整条构建分支。
    instance->index_builds = std::make_unique<IndexBuildService>();
    instance->executor->setIndexBuildService(instance->index_builds.get());
    instance->index_builds->startPeriodic(
        5000, [exec = instance->executor.get()] { folly::coro::blockingWait(exec->retryPendingIndexCleanups()); });

    return instance;
}

void GraphManager::checkpointAll() {
    std::shared_lock lock(mu_);
    for (auto& [name, inst] : graphs_) {
        auto t0 = std::chrono::steady_clock::now();
        {
            WtSession data_session = inst->data_conn.make_session();
            inst->data_conn.checkpoint(data_session);
        }
        // 统计快照与 journal：先发布快照，再压缩 journal（失败下次 checkpoint 重试）。
        if (inst->stats_store && inst->sync_data) {
            auto stats_snapshot = inst->stats_store->snapshot();
            // 先只持久化 snapshot；journal 压缩要求“snapshot 计数与 applied_seq 严格原子”，
            // 当前近似快照不满足，留给 Phase 2，避免提前删除导致恢复漏计。
            (void)inst->sync_data->saveStatsSnapshot(*stats_snapshot);
        }
        {
            WtSession meta_session = inst->meta_conn.make_session();
            inst->meta_conn.checkpoint(meta_session);
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        spdlog::info("Periodic checkpoint for graph '{}' completed ({}ms)", name, ms);
    }
    catalog_.checkpoint();
}

void GraphManager::checkpointLoop() {
    spdlog::info("Checkpoint thread started (interval {}s)", checkpoint_interval_sec_);
    while (running_.load()) {
        std::unique_lock<std::mutex> lk(checkpoint_mu_);
        checkpoint_cv_.wait_for(lk, std::chrono::seconds(checkpoint_interval_sec_),
                                [this] { return !running_.load(); });
        if (!running_.load())
            break;
        try {
            checkpointAll();
        } catch (const std::exception& e) {
            spdlog::error("Periodic checkpoint failed: {}", e.what());
        }
    }
    spdlog::info("Checkpoint thread stopped");
}

} // namespace eugraph
