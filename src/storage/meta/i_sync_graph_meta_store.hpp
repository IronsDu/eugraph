#pragma once

#include "common/types/graph_types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace eugraph {

/// Synchronous graph metadata storage interface.
/// Provides metadata raw KV operations and DDL for metadata-level management.
class ISyncGraphMetaStore {
public:
    virtual ~ISyncGraphMetaStore() = default;

    // ==================== Lifecycle ====================

    virtual bool open(const std::string& db_path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // ==================== Transaction ====================

    virtual GraphTxnHandle beginTransaction() = 0;
    virtual bool commitTransaction(GraphTxnHandle txn) = 0;
    virtual bool rollbackTransaction(GraphTxnHandle txn) = 0;

    // ==================== Metadata Raw KV ====================

    virtual bool metadataPut(std::string_view key, std::string_view value) = 0;
    virtual std::optional<std::string> metadataGet(std::string_view key) = 0;
    virtual bool metadataDel(std::string_view key) = 0;
    virtual void metadataScan(std::string_view prefix,
                              const std::function<bool(std::string_view, std::string_view)>& callback) = 0;

    // ==================== DDL ====================

    virtual bool createLabel(LabelId label_id) = 0;
    virtual bool dropLabel(LabelId label_id) = 0;
    virtual bool createEdgeLabel(EdgeLabelId edge_label_id) = 0;
    virtual bool dropEdgeLabel(EdgeLabelId edge_label_id) = 0;

    // ==================== Durability ====================

    virtual bool checkpoint() = 0;
};

} // namespace eugraph
