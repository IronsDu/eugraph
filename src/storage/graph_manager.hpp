#pragma once

#include "common/types/graph_types.hpp"
#include "query/executor/query_executor.hpp"
#include "storage/catalog/catalog_store.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

#include <folly/executors/CPUThreadPoolExecutor.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace eugraph {

struct GraphInstance {
    uint32_t graph_id;
    std::string name;
    std::unique_ptr<SyncGraphDataStore> sync_data;
    std::unique_ptr<SyncGraphMetaStore> sync_meta;
    std::unique_ptr<AsyncGraphDataStore> async_data;
    std::unique_ptr<AsyncGraphMetaStore> async_meta;
    std::unique_ptr<compute::QueryExecutor> executor;
};

class GraphManager {
public:
    static constexpr const char* kDefaultGraphName = "default";
    static constexpr int kDefaultCheckpointIntervalSec = 60;

    GraphManager() = default;
    ~GraphManager();

    GraphManager(const GraphManager&) = delete;
    GraphManager& operator=(const GraphManager&) = delete;

    /// `compute_pool` is the process-wide compute pool, normally created by the
    /// application entry point and shared by every graph's QueryExecutor and by
    /// the service layer. When null a private pool of `compute_threads` is
    /// created instead (used by tests).
    bool init(const std::string& data_dir, int io_threads, int compute_threads,
              int checkpoint_interval_sec = kDefaultCheckpointIntervalSec, const std::string& data_wt_config = "",
              std::shared_ptr<folly::CPUThreadPoolExecutor> compute_pool = nullptr);
    void shutdown();

    GraphEntry createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::vector<GraphEntry> listGraphs();

    GraphInstance* getGraph(const std::string& name);

    /// Compute pool shared by every graph's QueryExecutor. The Thrift server
    /// also uses it as its handler executor, so query execution and stream
    /// serialization run off the IO threads (mirroring what Bolt does).
    folly::Executor* computeExecutor() const {
        return compute_pool_.get();
    }

private:
    std::unique_ptr<GraphInstance> openGraphInstance(uint32_t graph_id, const std::string& name);
    void checkpointAll();
    void checkpointLoop();

    std::string data_dir_;
    int io_threads_ = 4;
    int checkpoint_interval_sec_ = kDefaultCheckpointIntervalSec;
    std::string data_wt_config_;
    std::shared_ptr<IoScheduler> io_scheduler_;
    // Shared by all graphs: one compute pool per process, not one per graph.
    std::shared_ptr<folly::CPUThreadPoolExecutor> compute_pool_;

    CatalogStore catalog_;

    std::shared_mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<GraphInstance>> graphs_;

    std::thread checkpoint_thread_;
    std::mutex checkpoint_mu_;
    std::condition_variable checkpoint_cv_;
    std::atomic<bool> running_{false};
};

} // namespace eugraph
