#include "storage/graph_manager.hpp"

#include <folly/coro/BlockingWait.h>
#include <spdlog/spdlog.h>

#include <filesystem>

namespace eugraph {

bool GraphManager::init(const std::string& data_dir, int io_threads, int compute_threads) {
    data_dir_ = data_dir;
    io_threads_ = io_threads;
    compute_threads_ = compute_threads;

    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    if (ec) {
        spdlog::error("Failed to create data directory: {}", ec.message());
        return false;
    }

    if (!catalog_.open(data_dir_ + "/catalog")) {
        spdlog::error("Failed to open catalog store");
        return false;
    }

    io_scheduler_ = std::make_shared<IoScheduler>(io_threads_);

    auto entries = catalog_.listGraphs();
    for (auto& entry : entries) {
        auto instance = openGraphInstance(entry.graph_id, entry.name);
        if (instance) {
            graphs_[entry.name] = std::move(instance);
            spdlog::info("Loaded graph '{}' (id={})", entry.name, entry.graph_id);
        } else {
            spdlog::warn("Failed to open graph '{}' (id={}), cleaning up catalog entry", entry.name, entry.graph_id);
            catalog_.dropGraph(entry.name);
        }
    }

    if (!catalog_.getGraph(kDefaultGraphName).has_value()) {
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

    return true;
}

void GraphManager::shutdown() {
    std::unique_lock lock(mu_);

    for (auto& [name, inst] : graphs_) {
        folly::coro::blockingWait(inst->async_meta->close());
        inst->sync_data->close();
        inst->sync_meta->close();
        spdlog::info("Closed graph '{}'", name);
    }
    graphs_.clear();
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

    std::unique_lock lock(mu_);

    auto it = graphs_.find(name);
    if (it == graphs_.end())
        throw std::runtime_error("Graph not found: " + name);

    folly::coro::blockingWait(it->second->async_meta->close());
    uint32_t graph_id = it->second->graph_id;
    it->second->sync_data->close();
    it->second->sync_meta->close();

    graphs_.erase(it);
    catalog_.dropGraph(name);

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

    compute::QueryExecutor::Config executor_config;
    executor_config.compute_threads = compute_threads_;
    instance->executor =
        std::make_unique<compute::QueryExecutor>(*instance->async_data, *instance->async_meta, executor_config);

    return instance;
}

} // namespace eugraph
