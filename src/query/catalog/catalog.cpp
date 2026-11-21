#include "query/catalog/catalog.hpp"

namespace eugraph {
namespace catalog {

void Catalog::load(std::shared_ptr<const GraphSchema> schema) {
    schema_ = std::move(schema);
    anon_label_id_ = INVALID_LABEL_ID;
    label_vertex_counts_.clear();
    if (!schema_)
        return;
    label_vertex_counts_ = schema_->label_vertex_counts;
    auto anon_it = schema_->label_name_to_id.find(std::string(kAnonLabelName));
    if (anon_it != schema_->label_name_to_id.end())
        anon_label_id_ = anon_it->second;
}

const LabelDef* Catalog::lookupLabel(const std::string& name) const {
    if (name == kAnonLabelName || !schema_)
        return nullptr;
    auto it = schema_->label_name_to_id.find(name);
    if (it == schema_->label_name_to_id.end())
        return nullptr;
    return lookupLabel(it->second);
}

const LabelDef* Catalog::lookupLabel(LabelId id) const {
    if (!schema_)
        return nullptr;
    auto it = schema_->labels.find(id);
    if (it == schema_->labels.end())
        return nullptr;
    return &it->second;
}

std::vector<const LabelDef*> Catalog::lookupLabels(const std::vector<std::string>& names) const {
    std::vector<const LabelDef*> result;
    result.reserve(names.size());
    for (const auto& name : names) {
        if (auto* def = lookupLabel(name))
            result.push_back(def);
    }
    return result;
}

const EdgeLabelDef* Catalog::lookupEdgeLabel(const std::string& name) const {
    if (!schema_)
        return nullptr;
    auto it = schema_->edge_label_name_to_id.find(name);
    if (it == schema_->edge_label_name_to_id.end())
        return nullptr;
    return lookupEdgeLabel(it->second);
}

const EdgeLabelDef* Catalog::lookupEdgeLabel(EdgeLabelId id) const {
    if (!schema_)
        return nullptr;
    auto it = schema_->edge_labels.find(id);
    if (it == schema_->edge_labels.end())
        return nullptr;
    return &it->second;
}

std::vector<EdgeLabelId> Catalog::resolveEdgeLabelIds(const std::vector<std::string>& names) const {
    std::vector<EdgeLabelId> result;
    result.reserve(names.size());
    if (!schema_)
        return result;
    for (const auto& name : names) {
        auto it = schema_->edge_label_name_to_id.find(name);
        if (it != schema_->edge_label_name_to_id.end())
            result.push_back(it->second);
    }
    return result;
}

const PropertyDef* Catalog::lookupProperty(LabelId lid, const std::string& prop_name) const {
    if (!schema_)
        return nullptr;
    auto lit = schema_->labels.find(lid);
    if (lit == schema_->labels.end())
        return nullptr;
    for (const auto& pd : lit->second.properties) {
        if (pd.name == prop_name)
            return &pd;
    }
    return nullptr;
}

const PropertyDef* Catalog::lookupEdgeLabelProperty(EdgeLabelId elid, const std::string& prop_name) const {
    if (!schema_)
        return nullptr;
    auto eit = schema_->edge_labels.find(elid);
    if (eit == schema_->edge_labels.end())
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
        if (auto* pd = lookupProperty(lid, prop_name))
            result.emplace_back(lid, pd);
    }
    return result;
}

const LabelDef::IndexDef* Catalog::lookupIndex(LabelId lid, const std::string& index_name) const {
    if (!schema_)
        return nullptr;
    auto lit = schema_->labels.find(lid);
    if (lit == schema_->labels.end())
        return nullptr;
    for (const auto& idx : lit->second.indexes) {
        if (idx.name == index_name)
            return &idx;
    }
    return nullptr;
}

LabelId Catalog::labelNameToId(const std::string& name) const {
    if (name == kAnonLabelName || !schema_)
        return INVALID_LABEL_ID;
    auto it = schema_->label_name_to_id.find(name);
    return it != schema_->label_name_to_id.end() ? it->second : INVALID_LABEL_ID;
}

EdgeLabelId Catalog::edgeLabelNameToId(const std::string& name) const {
    if (!schema_)
        return INVALID_EDGE_LABEL_ID;
    auto it = schema_->edge_label_name_to_id.find(name);
    return it != schema_->edge_label_name_to_id.end() ? it->second : INVALID_EDGE_LABEL_ID;
}

bool Catalog::hasIndex(LabelId lid, uint16_t prop_id) const {
    if (!schema_)
        return false;
    auto lit = schema_->labels.find(lid);
    if (lit == schema_->labels.end())
        return false;
    for (const auto& idx : lit->second.indexes) {
        for (uint16_t pid : idx.prop_ids) {
            if (pid == prop_id)
                return true;
        }
    }
    return false;
}

} // namespace catalog
} // namespace eugraph
