#pragma once

#include "common/types/constants.hpp"
#include "storage/meta/i_sync_graph_meta_store.hpp"
#include "storage/wt_store_base.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

#include <wiredtiger.h>

namespace eugraph {

/// ISyncGraphMetaStore implementation using WiredTiger.
/// Owns its WT connection for the metadata table.
class SyncGraphMetaStore : public WtStoreBase, public ISyncGraphMetaStore {
public:
    SyncGraphMetaStore() = default;
    ~SyncGraphMetaStore() override;

    SyncGraphMetaStore(const SyncGraphMetaStore&) = delete;
    SyncGraphMetaStore& operator=(const SyncGraphMetaStore&) = delete;

    // Lifecycle
    bool open(const std::string& db_path) override;
    /// Same, with extra WiredTiger config (diagnostics / tuning). The data store has
    /// had this for a while; the meta store did not, so a WT problem on the meta side
    /// (which has its own log server) could not be instrumented at all.
    bool open(const std::string& db_path, const std::string& wt_extra_config);
    void close() override;
    bool isOpen() const override;

    // Transaction
    GraphTxnHandle beginTransaction() override;
    bool commitTransaction(GraphTxnHandle txn) override;
    bool rollbackTransaction(GraphTxnHandle txn) override;

    // Metadata Raw KV
    bool metadataPut(std::string_view key, std::string_view value) override;
    std::optional<std::string> metadataGet(std::string_view key) override;
    bool metadataDel(std::string_view key) override;
    void metadataScan(std::string_view prefix,
                      const std::function<bool(std::string_view, std::string_view)>& callback) override;

    // DDL — metadata-level operations
    bool createLabel(LabelId label_id) override;
    bool dropLabel(LabelId label_id) override;
    bool createEdgeLabel(EdgeLabelId edge_label_id) override;
    bool addEdgeLabelProperties(EdgeLabelId edge_label_id, const std::vector<std::string>& prop_names) override;
    bool dropEdgeLabel(EdgeLabelId edge_label_id) override;

    // Durability
    bool checkpoint() override;
};

} // namespace eugraph
