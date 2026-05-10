#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/query_executor.hpp"
#include "storage/catalog/catalog_store.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
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

    GraphManager() = default;
    ~GraphManager() = default;

    GraphManager(const GraphManager&) = delete;
    GraphManager& operator=(const GraphManager&) = delete;

    bool init(const std::string& data_dir, int io_threads, int compute_threads);
    void shutdown();

    GraphEntry createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::vector<GraphEntry> listGraphs();

    GraphInstance* getGraph(const std::string& name);

private:
    std::unique_ptr<GraphInstance> openGraphInstance(uint32_t graph_id, const std::string& name);

    std::string data_dir_;
    int io_threads_ = 4;
    int compute_threads_ = 4;
    std::shared_ptr<IoScheduler> io_scheduler_;

    CatalogStore catalog_;

    std::shared_mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<GraphInstance>> graphs_;
};

} // namespace eugraph
