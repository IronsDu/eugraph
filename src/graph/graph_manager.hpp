#pragma once

#include "common/concurrency/graph_usage_gate.hpp"
#include "common/types/graph_types.hpp"
#include "query/executor/query_executor.hpp"
#include "storage/catalog/catalog_store.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/index/index_build_service.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"
#include "storage/session/schema_generation_tracker.hpp"
#include "storage/session/session_pool.hpp"
#include "storage/stats/graph_stats_store.hpp"
#include "storage/wt_connection_store.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace eugraph {

struct GraphInstance {
    /// 使用闸门：DROP DATABASE 必须先关闸并等在飞使用者归零，再关闭 WT 连接（否则 WT PANIC）
    std::shared_ptr<GraphUsageGate> usage = std::make_shared<GraphUsageGate>();
    uint32_t graph_id;
    std::string name;
    /// 连接所有者（§3.5 第 7 条 ✓）：声明在 store 之前 ⇒ **晚于** store 销毁 ✓
    WtConnectionStore data_conn;
    WtConnectionStore meta_conn;
    std::unique_ptr<SyncGraphDataStore> sync_data;
    std::unique_ptr<SyncGraphMetaStore> sync_meta;
    /// 共享 async 门面的数据会话锚点（只有 for_session() 会用它，数据 op 用语句会话）。
    std::unique_ptr<GraphSession> async_session;
    std::unique_ptr<AsyncGraphDataStore> async_data;
    std::unique_ptr<AsyncGraphMetaStore> async_meta;
    /// CBO 统计（每图一份；启动为空/近似，后续从 journal/重建填充）。
    std::shared_ptr<GraphStatsStore> stats_store;

    /// v2 索引运行时注册表（每图一份）。executor/算子只持裸指针 ⇒ **必须先于 executor 声明**（后销毁）。
    std::shared_ptr<IndexRuntimeRegistry> index_registry;
    /// schema 代数 pin 计数（DROP 等"旧版本事务"用）。
    std::shared_ptr<SchemaGenerationTracker> schema_generations;

    /// 会话池（图级 ✓）：**声明在最后 ⇒ 最先销毁** ✓ ⇒ 池内 session 在 WT 连接关闭前被 close ✓（§3.2.1）
    std::shared_ptr<SessionPool> session_pool;
    /// 查询执行器（持有 pool/stores 引用）：声明在 pool 之后 ⇒ **比 pool 先销毁** ✓
    std::unique_ptr<compute::QueryExecutor> executor;
    /// 每图索引构建服务：**声明在 executor 之后** ⇒ 先 shutdown/析构（Job 捕获 executor 指针）。
    std::unique_ptr<IndexBuildService> index_builds;

    ~GraphInstance() {
        if (index_builds)
            index_builds->shutdown();
    }
};

/// 图实例**生命周期租约**：持有期间 DROP 会关闸并等待租约归零后再销毁连接。
struct GraphLease {
    GraphInstance* inst = nullptr;
    GraphUsageGate::Guard guard;

    explicit operator bool() const {
        return inst != nullptr;
    }
    GraphInstance* operator->() const {
        return inst;
    }
};

class GraphManager {
public:
    static constexpr const char* kDefaultGraphName = "default";
    static constexpr int kDefaultCheckpointIntervalSec = 60;

    GraphManager() = default;
    ~GraphManager();

    GraphManager(const GraphManager&) = delete;
    GraphManager& operator=(const GraphManager&) = delete;

    bool init(const std::string& data_dir, int io_threads, int compute_threads,
              int checkpoint_interval_sec = kDefaultCheckpointIntervalSec, const std::string& data_wt_config = "");
    void shutdown();

    /// Shared query-compute pool (one for the whole process): Bolt session
    /// handling, and Thrift query planning / operator execution. The Thrift
    /// handler and stream loop stay on the network IO pool (fbthrift
    /// WriteBatcher constraint, see docs/query/engine/execution-model.md §3).
    folly::Executor* computeExecutor() const {
        return compute_pool_.get();
    }

    GraphEntry createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::vector<GraphEntry> listGraphs();

    /// 在 shared_lock 内完成“查表 + tryEnter” ⇒ 返回的指针在 guard 存活期间有效 ✓
    /// 图不存在 / 正在 DROP ⇒ nullopt
    std::optional<GraphLease> acquireGraph(const std::string& name);

private:
    /// Opens a graph's stores; never throws (logs and returns nullptr on failure).
    std::unique_ptr<GraphInstance> openGraphInstance(uint32_t graph_id, const std::string& name);

    /// The actual open path; may throw (decoders reject an incompatible on-disk format).
    std::unique_ptr<GraphInstance> openGraphInstanceUnchecked(uint32_t graph_id, const std::string& name);
    void checkpointAll();
    void checkpointLoop();

    std::string data_dir_;
    int io_threads_ = 4;
    int compute_threads_ = 4;
    int checkpoint_interval_sec_ = kDefaultCheckpointIntervalSec;
    std::string data_wt_config_;
    std::shared_ptr<IoScheduler> io_scheduler_;
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
