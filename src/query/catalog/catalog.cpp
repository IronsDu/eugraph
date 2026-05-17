#include "query/catalog/catalog.hpp"

namespace eugraph {
namespace catalog {

void Catalog::load(std::unordered_map<LabelId, LabelDef> label_defs,
                   std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs) {
    label_defs_ = std::move(label_defs);
    edge_label_defs_ = std::move(edge_label_defs);

    label_name_to_id_.clear();
    anon_label_id_ = INVALID_LABEL_ID;
    for (const auto& [lid, ldef] : label_defs_) {
        label_name_to_id_[ldef.name] = lid;
    }
    // Identify __anon__ label and remove from user-visible name map
    auto anon_it = label_name_to_id_.find("__anon__");
    if (anon_it != label_name_to_id_.end()) {
        anon_label_id_ = anon_it->second;
        label_name_to_id_.erase(anon_it);
    }
    edge_label_name_to_id_.clear();
    for (const auto& [elid, eldef] : edge_label_defs_) {
        edge_label_name_to_id_[eldef.name] = elid;
    }
}

const LabelDef* Catalog::lookupLabel(const std::string& name) const {
    auto it = label_name_to_id_.find(name);
    if (it == label_name_to_id_.end())
        return nullptr;
    return lookupLabel(it->second);
}

const LabelDef* Catalog::lookupLabel(LabelId id) const {
    auto it = label_defs_.find(id);
    if (it == label_defs_.end())
        return nullptr;
    return &it->second;
}

std::vector<const LabelDef*> Catalog::lookupLabels(const std::vector<std::string>& names) const {
    std::vector<const LabelDef*> result;
    result.reserve(names.size());
    for (const auto& name : names) {
        if (auto* def = lookupLabel(name)) {
            result.push_back(def);
        }
    }
    return result;
}

const EdgeLabelDef* Catalog::lookupEdgeLabel(const std::string& name) const {
    auto it = edge_label_name_to_id_.find(name);
    if (it == edge_label_name_to_id_.end())
        return nullptr;
    return lookupEdgeLabel(it->second);
}

const EdgeLabelDef* Catalog::lookupEdgeLabel(EdgeLabelId id) const {
    auto it = edge_label_defs_.find(id);
    if (it == edge_label_defs_.end())
        return nullptr;
    return &it->second;
}

std::vector<EdgeLabelId> Catalog::resolveEdgeLabelIds(const std::vector<std::string>& names) const {
    std::vector<EdgeLabelId> result;
    result.reserve(names.size());
    for (const auto& name : names) {
        auto it = edge_label_name_to_id_.find(name);
        if (it != edge_label_name_to_id_.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

const PropertyDef* Catalog::lookupProperty(LabelId lid, const std::string& prop_name) const {
    auto lit = label_defs_.find(lid);
    if (lit == label_defs_.end())
        return nullptr;
    for (const auto& pd : lit->second.properties) {
        if (pd.name == prop_name)
            return &pd;
    }
    return nullptr;
}

const PropertyDef* Catalog::lookupEdgeLabelProperty(EdgeLabelId elid, const std::string& prop_name) const {
    auto eit = edge_label_defs_.find(elid);
    if (eit == edge_label_defs_.end())
        return nullptr;
    for (const auto& pd : eit->second.properties) {
        if (pd.name == prop_name)
            return &pd;
    }
    return nullptr;
}

std::vector<std::pair<LabelId, const PropertyDef*>>
Catalog::lookupPropertyAcrossLabels(const LabelIdSet& label_ids, const std::string& prop_name) const {
    std::vector<std::pair<LabelId, const PropertyDef*>> result;
    for (LabelId lid : label_ids) {
        if (auto* pd = lookupProperty(lid, prop_name)) {
            result.emplace_back(lid, pd);
        }
    }
    return result;
}

const LabelDef::IndexDef* Catalog::lookupIndex(LabelId lid, const std::string& index_name) const {
    auto lit = label_defs_.find(lid);
    if (lit == label_defs_.end())
        return nullptr;
    for (const auto& idx : lit->second.indexes) {
        if (idx.name == index_name)
            return &idx;
    }
    return nullptr;
}

LabelId Catalog::labelNameToId(const std::string& name) const {
    auto it = label_name_to_id_.find(name);
    return it != label_name_to_id_.end() ? it->second : INVALID_LABEL_ID;
}

EdgeLabelId Catalog::edgeLabelNameToId(const std::string& name) const {
    auto it = edge_label_name_to_id_.find(name);
    return it != edge_label_name_to_id_.end() ? it->second : INVALID_EDGE_LABEL_ID;
}

} // namespace catalog
} // namespace eugraph
