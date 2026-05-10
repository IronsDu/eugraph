#pragma once

#include "storage/wt_store_base.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {

struct GraphEntry {
    uint32_t graph_id;
    std::string name;
    uint64_t created_at;
};

class CatalogStore : public WtStoreBase {
public:
    CatalogStore() = default;
    ~CatalogStore() override;

    CatalogStore(const CatalogStore&) = delete;
    CatalogStore& operator=(const CatalogStore&) = delete;

    bool open(const std::string& db_path);
    void close();

    std::optional<GraphEntry> createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::optional<GraphEntry> getGraph(const std::string& name);
    std::vector<GraphEntry> listGraphs();

private:
    static constexpr std::string_view kGraphPrefix = "G|";
    static constexpr std::string_view kNextIdKey = "N|next_graph_id";

    std::string encodeGraphEntry(const GraphEntry& entry);
    GraphEntry decodeGraphEntry(std::string_view data);

    uint32_t allocateNextId();
};

} // namespace eugraph
