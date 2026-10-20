#pragma once

#include "storage/schema_ddl.hpp"
#include "storage/wt_connection_store.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {

struct GraphEntry {
    uint32_t graph_id;
    std::string name;
    uint64_t created_at;
};

class CatalogStore {
public:
    CatalogStore() = default;
    ~CatalogStore();

    CatalogStore(const CatalogStore&) = delete;
    CatalogStore& operator=(const CatalogStore&) = delete;

    bool open(const std::string& db_path);
    void close();
    bool checkpoint() {
        WtSession session = conn_.make_session();
        return conn_.checkpoint(session);
    }

    std::optional<GraphEntry> createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::optional<GraphEntry> getGraph(const std::string& name);
    std::vector<GraphEntry> listGraphs();

private:
    WtConnectionStore conn_;
    SchemaDdl schema_ddl_;
    static constexpr std::string_view kGraphPrefix = "G|";
    static constexpr std::string_view kNextIdKey = "N|next_graph_id";

    std::string encodeGraphEntry(const GraphEntry& entry);
    GraphEntry decodeGraphEntry(std::string_view data);

    uint32_t allocateNextId();
};

} // namespace eugraph
