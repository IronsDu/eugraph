#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/query_executor.hpp"
#include "gen-cpp2/EuGraphService.h"
#include "metadata_service/metadata_service.hpp"
#include "storage/graph_store.hpp"

namespace eugraph {
namespace server {

/// Service handler that implements the Thrift-generated EuGraphService interface.
/// Used both in embedded mode (direct calls) and RPC mode (via fbthrift server).
class EuGraphHandler : public apache::thrift::ServiceHandler<thrift::EuGraphService> {
public:
    EuGraphHandler(IGraphStore& store, IMetadataService& meta, compute::QueryExecutor& executor)
        : store_(store), meta_(meta), executor_(executor) {}

    // Thrift service methods (sync_ interface)
    void sync_createLabel(thrift::LabelInfo& _return, std::unique_ptr<std::string> name,
                          std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) override;

    void sync_listLabels(std::vector<thrift::LabelInfo>& _return) override;

    void sync_createEdgeLabel(thrift::EdgeLabelInfo& _return, std::unique_ptr<std::string> name,
                              std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) override;

    void sync_listEdgeLabels(std::vector<thrift::EdgeLabelInfo>& _return) override;

    void sync_executeCypher(thrift::QueryResult& _return, std::unique_ptr<std::string> query) override;

private:
    // Convert runtime Value to Thrift ResultValue union
    thrift::ResultValue valueToThrift(const Value& val);

    // Convert Thrift PropertyType enum to internal PropertyType
    static ::eugraph::PropertyType toPropertyType(thrift::PropertyType t);

    // Convert Thrift PropertyDefThrift to internal PropertyDef
    static PropertyDef toPropertyDef(const thrift::PropertyDefThrift& req, uint16_t id);

    // Format properties for display
    static std::string formatProperties(const std::vector<PropertyDef>& props);

    IGraphStore& store_;
    IMetadataService& meta_;
    compute::QueryExecutor& executor_;
};

} // namespace server
} // namespace eugraph
