#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace eugraph {

/// Resolution of one property's "homes" on a single vertex.
///
/// A home is:
///   - a label the vertex actually holds that DECLARES the property (the
///     declared column exists even when its value is NULL), or
///   - the weak space (`__anon__`) when the vertex has a VALUE there.
///
/// More than one home is ambiguity: the weak view has no single value.
/// See docs/features/strong-weak-hybrid.md.
struct PropertyHome {
    int count = 0;
    /// The single declared home label (INVALID when there is none).
    LabelId label = INVALID_LABEL_ID;
    /// True when the single home is the weak space.
    bool anon = false;
    /// Value observed for the single home (nullopt when absent / ambiguous).
    std::optional<PropertyValue> value;
};

inline const PropertyDef* findPropertyDefByName(const LabelDef& def, const std::string& prop_name) {
    for (const auto& pd : def.properties) {
        if (pd.name == prop_name)
            return &pd;
    }
    return nullptr;
}

/// Resolve the homes of `prop_name` for a vertex whose full label set is
/// `held_labels` and whose loaded properties are in `props`.
///
/// Callers must pass the COMPLETE held label set (VertexValue.labels), not just
/// the labels whose properties happen to be loaded: a declared column counts as
/// a home even when no value was materialized.
template <typename LabelDefLookup>
inline PropertyHome resolvePropertyHome(LabelDefLookup&& lookup, const LabelIdSet& held_labels,
                                        const std::unordered_map<LabelId, Properties>& props,
                                        const std::string& prop_name, LabelId anon_label_id) {
    PropertyHome home;
    for (LabelId lid : held_labels) {
        if (lid == anon_label_id)
            continue;
        const LabelDef* def = lookup(lid);
        if (!def)
            continue;
        const PropertyDef* pd = findPropertyDefByName(*def, prop_name);
        if (!pd)
            continue;
        home.count++;
        home.label = lid;
        auto pit = props.find(lid);
        if (pit != props.end() && pd->id < pit->second.size() && pit->second[pd->id].has_value())
            home.value = pit->second[pd->id].value();
    }
    if (anon_label_id != INVALID_LABEL_ID && held_labels.count(anon_label_id)) {
        const LabelDef* def = lookup(anon_label_id);
        auto pit = props.find(anon_label_id);
        if (def && pit != props.end()) {
            const PropertyDef* pd = findPropertyDefByName(*def, prop_name);
            if (pd && pd->id < pit->second.size() && pit->second[pd->id].has_value()) {
                home.count++;
                home.label = anon_label_id;
                home.anon = true;
                home.value = pit->second[pd->id].value();
            }
        }
    }
    return home;
}

} // namespace eugraph
