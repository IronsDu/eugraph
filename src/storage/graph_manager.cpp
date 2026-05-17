#include "storage/graph_manager.hpp"

#include <folly/coro/BlockingWait.h>
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

bool GraphManager::init(const std::string& data_dir, int io_threads, int compute_threads, int checkpoint_interval_sec) {
    data_dir_ = data_dir;
    io_threads_ = io_threads;
    compute_threads_ = compute_threads;
    checkpoint_interval_sec_ = checkpoint_interval_sec;

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
            spdlog::warn("Failed to open graph '{}' (id={}) in {}ms, cleaning up catalog entry", entry.name,
                         entry.graph_id, ms);
            catalog_.dropGraph(entry.name);
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

    for (auto& inst : instances) {
        folly::coro::blockingWait(inst->async_meta->close());
        inst->sync_data->close();
        inst->sync_meta->close();
    }
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
    // lock released — safe to block on I/O; inst owns the GraphInstance

    folly::coro::blockingWait(inst->async_meta->close());
    inst->sync_data->close();
    inst->sync_meta->close();

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

GraphInstance* GraphManager::getGraph(const std::string& name) {
    std::shared_lock lock(mu_);
    auto it = graphs_.find(name);
    return it != graphs_.end() ? it->second.get() : nullptr;
}

std::unique_ptr<GraphInstance> GraphManager::openGraphInstance(uint32_t graph_id, const std::string& name) {
    std::string graph_dir = data_dir_ + "/graph_" + std::to_string(graph_id);
    std::string data_dir = graph_dir + "/data";
    std::string meta_dir = graph_dir + "/meta";

    auto instance = std::make_unique<GraphInstance>();
    instance->graph_id = graph_id;
    instance->name = name;

    instance->sync_data = std::make_unique<SyncGraphDataStore>();
    if (!instance->sync_data->open(data_dir)) {
        spdlog::error("Failed to open data store for graph '{}' at {}", name, data_dir);
        return nullptr;
    }

    instance->sync_meta = std::make_unique<SyncGraphMetaStore>();
    if (!instance->sync_meta->open(meta_dir)) {
        spdlog::error("Failed to open meta store for graph '{}' at {}", name, meta_dir);
        return nullptr;
    }

    instance->async_data = std::make_unique<AsyncGraphDataStore>(*instance->sync_data, *io_scheduler_);

    instance->async_meta = std::make_unique<AsyncGraphMetaStore>();
    auto opened = folly::coro::blockingWait(instance->async_meta->open(*instance->sync_meta, *io_scheduler_));
    if (!opened) {
        spdlog::error("Failed to initialize async meta store for graph '{}'", name);
        return nullptr;
    }

    // Ensure __anon__ label exists for unlabeled node properties
    {
        auto anon_def = folly::coro::blockingWait(instance->async_meta->getLabelDef("__anon__"));
        if (!anon_def.has_value()) {
            auto anon_id = folly::coro::blockingWait(instance->async_meta->createLabel("__anon__", {}));
            if (anon_id == INVALID_LABEL_ID) {
                spdlog::error("Failed to create __anon__ label for graph '{}'", name);
                return nullptr;
            }
            folly::coro::blockingWait(instance->async_data->createLabel(anon_id));
            spdlog::info("Created __anon__ label (id={}) for graph '{}'", anon_id, name);
        }
    }

    compute::QueryExecutor::Config executor_config;
    executor_config.compute_threads = compute_threads_;
    instance->executor =
        std::make_unique<compute::QueryExecutor>(*instance->async_data, *instance->async_meta, executor_config);

    return instance;
}

void GraphManager::checkpointAll() {
    std::shared_lock lock(mu_);
    for (auto& [name, inst] : graphs_) {
        auto t0 = std::chrono::steady_clock::now();
        inst->sync_data->checkpoint();
        inst->sync_meta->checkpoint();
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
