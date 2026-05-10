#include "storage/catalog/catalog_store.hpp"

#include "common/types/constants.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>

namespace eugraph {

CatalogStore::~CatalogStore() {
    close();
}

bool CatalogStore::open(const std::string& db_path) {
    if (!openConnection(db_path))
        return false;

    auto* session = defaultSession_.get();
    if (!ensureGlobalTable(session, TABLE_METADATA))
        return false;

    return true;
}

void CatalogStore::close() {
    closeConnection();
}

std::optional<GraphEntry> CatalogStore::createGraph(const std::string& name) {
    std::string key = std::string(kGraphPrefix) + name;
    auto existing = tableGet(defaultSession_.get(), TABLE_METADATA, key);
    if (existing.has_value())
        return std::nullopt;

    uint32_t id = allocateNextId();
    if (id == UINT32_MAX)
        return std::nullopt;

    GraphEntry entry;
    entry.graph_id = id;
    entry.name = name;
    auto now = std::chrono::system_clock::now();
    entry.created_at = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    std::string value = encodeGraphEntry(entry);
    if (!tablePut(defaultSession_.get(), TABLE_METADATA, key, value))
        return std::nullopt;

    return entry;
}

bool CatalogStore::dropGraph(const std::string& name) {
    std::string key = std::string(kGraphPrefix) + name;
    return tableDel(defaultSession_.get(), TABLE_METADATA, key);
}

std::optional<GraphEntry> CatalogStore::getGraph(const std::string& name) {
    std::string key = std::string(kGraphPrefix) + name;
    auto data = tableGet(defaultSession_.get(), TABLE_METADATA, key);
    if (!data.has_value())
        return std::nullopt;
    return decodeGraphEntry(*data);
}

std::vector<GraphEntry> CatalogStore::listGraphs() {
    std::vector<GraphEntry> entries;
    tableScan(defaultSession_.get(), TABLE_METADATA, kGraphPrefix,
              [&](std::string_view key, std::string_view value) {
                  auto entry = decodeGraphEntry(value);
                  entry.name = std::string(key.substr(kGraphPrefix.size()));
                  entries.push_back(std::move(entry));
                  return true;
              });
    return entries;
}

std::string CatalogStore::encodeGraphEntry(const GraphEntry& entry) {
    std::string buf;
    buf.resize(4 + 8);
    uint32_t id_be = __builtin_bswap32(entry.graph_id);
    uint64_t ts_be = __builtin_bswap64(entry.created_at);
    std::memcpy(buf.data(), &id_be, 4);
    std::memcpy(buf.data() + 4, &ts_be, 8);
    return buf;
}

GraphEntry CatalogStore::decodeGraphEntry(std::string_view data) {
    GraphEntry entry;
    uint32_t id_be;
    uint64_t ts_be;
    std::memcpy(&id_be, data.data(), 4);
    std::memcpy(&ts_be, data.data() + 4, 8);
    entry.graph_id = __builtin_bswap32(id_be);
    entry.created_at = __builtin_bswap64(ts_be);
    return entry;
}

uint32_t CatalogStore::allocateNextId() {
    auto existing = tableGet(defaultSession_.get(), TABLE_METADATA, std::string(kNextIdKey));
    uint32_t next_id = 0;
    if (existing.has_value() && existing->size() >= 4) {
        uint32_t val;
        std::memcpy(&val, existing->data(), 4);
        next_id = __builtin_bswap32(val);
    }

    uint32_t new_id = next_id;
    uint32_t new_next = next_id + 1;
    uint32_t new_next_be = __builtin_bswap32(new_next);
    std::string val_buf(reinterpret_cast<const char*>(&new_next_be), 4);
    if (!tablePut(defaultSession_.get(), TABLE_METADATA, std::string(kNextIdKey), val_buf))
        return UINT32_MAX;

    return new_id;
}

} // namespace eugraph
