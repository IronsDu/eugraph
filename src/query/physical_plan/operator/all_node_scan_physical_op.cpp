#include "query/physical_plan/operator/all_node_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include <spdlog/spdlog.h>

#include <unordered_set>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> AllNodeScanPhysicalOp::executeChunk() {
    std::unordered_map<VertexId, VertexValue> vertex_map;
    for (const auto& [name, label_id] : label_map_) {
        if (label_id == INVALID_LABEL_ID)
            continue;
        auto gen = store_.scanVerticesByLabel(label_id);
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch) {
                auto& vv = vertex_map[vid];
                if (vv.id == INVALID_VERTEX_ID) {
                    vv.id = vid;
                    vv.labels = co_await store_.getVertexLabels(vid);
                    if (vv.labels && anon_label_id_ != INVALID_LABEL_ID) {
                        vv.labels->erase(anon_label_id_);
                    }
                    // Load all properties from every label the vertex belongs
                    // to.  A property may be registered on the __anon__ label
                    // by the binder/ALTER but physically stored under the
                    // vertex's concrete label (e.g. A).  Without scanning
                    // every label the BoundPropertyRef evaluator would fail
                    // to find the value.
                    if (vv.labels) {
                        for (LabelId lid : *vv.labels) {
                            auto def_it = label_defs_.find(lid);
                            if (def_it != label_defs_.end() && !def_it->second.properties.empty()) {
                                std::vector<uint16_t> pids;
                                for (const auto& pd : def_it->second.properties)
                                    pids.push_back(pd.id);
                                auto props = co_await store_.getVertexProperties(vid, lid, pids);
                                if (props)
                                    vv.properties[lid] = std::move(*props);
                            }
                        }
                    }
                    if (anon_label_id_ != INVALID_LABEL_ID) {
                        auto anon_def_it = label_defs_.find(anon_label_id_);
                        if (anon_def_it != label_defs_.end() && !anon_def_it->second.properties.empty()) {
                            std::vector<uint16_t> anon_pids;
                            for (const auto& pd : anon_def_it->second.properties)
                                anon_pids.push_back(pd.id);
                            auto anon_props = co_await store_.getVertexProperties(vid, anon_label_id_, anon_pids);
                            if (anon_props && !anon_props->empty())
                                vv.properties[anon_label_id_] = std::move(*anon_props);
                        }
                    }
                }
                auto it = label_prop_ids_.find(label_id);
                if (it != label_prop_ids_.end() && !it->second.empty()) {
                    auto props = co_await store_.getVertexProperties(vid, label_id, it->second);
                    if (props) {
                        vv.properties[label_id] = std::move(*props);
                    }
                }
            }
        }
    }

    spdlog::info("[AllNodeScan] vertex_map size={}, anon_label_id_={}", vertex_map.size(), anon_label_id_);
    for (const auto& [lid, def] : label_defs_) {
        spdlog::info("[AllNodeScan] label_def: lid={} name={} props={}", lid, def.name, def.properties.size());
        for (const auto& pd : def.properties)
            spdlog::info("[AllNodeScan]   prop: id={} name={}", pd.id, pd.name);
    }
    for (const auto& [lid, pids] : label_prop_ids_)
        spdlog::info("[AllNodeScan] label_prop_ids: lid={} pids_count={}", lid, pids.size());

    DataChunk chunk;
    chunk.setSchema(output_types_);
    chunk.reserve(DataChunk::DEFAULT_CAPACITY);

    for (auto& [vid, vv] : vertex_map) {
        std::string props_desc;
        for (const auto& [lid, pvec] : vv.properties) {
            props_desc += "lid=" + std::to_string(lid) + ":[";
            for (size_t i = 0; i < pvec.size(); ++i) {
                if (pvec[i].has_value()) {
                    if (std::holds_alternative<std::string>(*pvec[i]))
                        props_desc += std::get<std::string>(*pvec[i]);
                    else if (std::holds_alternative<int64_t>(*pvec[i]))
                        props_desc += std::to_string(std::get<int64_t>(*pvec[i]));
                }
                props_desc += ",";
            }
            props_desc += "] ";
        }
        spdlog::info("[AllNodeScan] vid={} labels_count={} props={}", vid,
                     vv.labels ? vv.labels->size() : 0, props_desc);
        chunk.appendRow({Value(std::move(vv))});
        if (chunk.count >= DataChunk::DEFAULT_CAPACITY) {
            co_yield std::move(chunk);
            chunk = DataChunk{};
            chunk.setSchema(output_types_);
            chunk.reserve(DataChunk::DEFAULT_CAPACITY);
        }
    }
    if (chunk.count > 0) {
        co_yield std::move(chunk);
    }
}

} // namespace compute
} // namespace eugraph
