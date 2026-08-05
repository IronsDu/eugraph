#include "bolt/bolt_value_mapping.hpp"

#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"

#include <string>

namespace eugraph {
namespace bolt {

using PS = packstream::PackStreamValueStorage;

namespace {

packstream::Value propertyToBolt(const PropertyValue& pv) {
    if (std::holds_alternative<std::monostate>(pv)) {
        return std::monostate{};
    } else if (std::holds_alternative<bool>(pv)) {
        return std::get<bool>(pv);
    } else if (std::holds_alternative<int64_t>(pv)) {
        return std::get<int64_t>(pv);
    } else if (std::holds_alternative<double>(pv)) {
        return std::get<double>(pv);
    } else if (std::holds_alternative<std::string>(pv)) {
        return std::get<std::string>(pv);
    } else if (std::holds_alternative<std::vector<int64_t>>(pv)) {
        std::vector<PS> list;
        for (auto x : std::get<std::vector<int64_t>>(pv))
            list.push_back(PS{static_cast<int64_t>(x)});
        return list;
    } else if (std::holds_alternative<std::vector<double>>(pv)) {
        std::vector<PS> list;
        for (auto x : std::get<std::vector<double>>(pv))
            list.push_back(PS{x});
        return list;
    } else if (std::holds_alternative<std::vector<std::string>>(pv)) {
        std::vector<PS> list;
        for (auto& s : std::get<std::vector<std::string>>(pv))
            list.push_back(PS{s});
        return list;
    } else if (std::holds_alternative<DateTimeValue>(pv)) {
        return temporalToString(std::get<DateTimeValue>(pv));
    } else if (std::holds_alternative<TimeValue>(pv)) {
        return temporalToString(std::get<TimeValue>(pv));
    } else if (std::holds_alternative<DurationValue>(pv)) {
        return temporalToString(std::get<DurationValue>(pv));
    } else if (std::holds_alternative<std::vector<DateTimeValue>>(pv)) {
        std::vector<PS> list;
        for (auto& tv : std::get<std::vector<DateTimeValue>>(pv))
            list.push_back(PS{temporalToString(tv)});
        return list;
    } else if (std::holds_alternative<std::vector<TimeValue>>(pv)) {
        std::vector<PS> list;
        for (auto& tv : std::get<std::vector<TimeValue>>(pv))
            list.push_back(PS{temporalToString(tv)});
        return list;
    } else if (std::holds_alternative<std::vector<DurationValue>>(pv)) {
        std::vector<PS> list;
        for (auto& dv : std::get<std::vector<DurationValue>>(pv))
            list.push_back(PS{temporalToString(dv)});
        return list;
    }
    return std::monostate{};
}

} // anonymous namespace

packstream::Value valueToBolt(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                              const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs) {
    if (std::holds_alternative<std::monostate>(val)) {
        return std::monostate{};
    } else if (std::holds_alternative<bool>(val)) {
        return std::get<bool>(val);
    } else if (std::holds_alternative<int64_t>(val)) {
        return std::get<int64_t>(val);
    } else if (std::holds_alternative<double>(val)) {
        return std::get<double>(val);
    } else if (std::holds_alternative<std::string>(val)) {
        return std::get<std::string>(val);
    } else if (std::holds_alternative<VertexValue>(val)) {
        auto& v = std::get<VertexValue>(val);

        std::vector<PS> label_list;
        if (v.labels.has_value()) {
            for (LabelId lid : *v.labels) {
                auto it = label_defs.find(lid);
                if (it != label_defs.end() && it->second.name != kAnonLabelName)
                    label_list.push_back(PS{it->second.name});
            }
        }

        std::unordered_map<std::string, PS> props;
        for (const auto& [lid, props_vec] : v.properties) {
            auto it = label_defs.find(lid);
            if (it == label_defs.end())
                continue;
            for (const auto& pd : it->second.properties) {
                if (pd.id < props_vec.size()) {
                    const auto& pv = props_vec[pd.id];
                    if (pv.has_value())
                        props[pd.name] = PS{propertyToBolt(*pv)};
                }
            }
        }

        packstream::PackStreamStruct node_s;
        node_s.tag = tags::NODE;
        node_s.fields.push_back(PS{static_cast<int64_t>(v.id)});
        node_s.fields.push_back(PS{std::move(label_list)});
        node_s.fields.push_back(PS{std::move(props)});
        return node_s;
    } else if (std::holds_alternative<EdgeValue>(val)) {
        auto& e = std::get<EdgeValue>(val);

        std::string type_name;
        auto elit = edge_label_defs.find(e.label_id);
        if (elit != edge_label_defs.end())
            type_name = elit->second.name;

        std::unordered_map<std::string, PS> props;
        if (e.properties.has_value() && elit != edge_label_defs.end()) {
            for (const auto& pd : elit->second.properties) {
                if (pd.id < e.properties->size()) {
                    const auto& pv = (*e.properties)[pd.id];
                    if (pv.has_value())
                        props[pd.name] = PS{propertyToBolt(*pv)};
                }
            }
        }

        packstream::PackStreamStruct rel_s;
        rel_s.tag = tags::RELATIONSHIP;
        rel_s.fields.push_back(PS{static_cast<int64_t>(e.id)});
        rel_s.fields.push_back(PS{static_cast<int64_t>(e.src_id)});
        rel_s.fields.push_back(PS{static_cast<int64_t>(e.dst_id)});
        rel_s.fields.push_back(PS{std::move(type_name)});
        rel_s.fields.push_back(PS{std::move(props)});
        return rel_s;
    } else if (std::holds_alternative<PathValue>(val)) {
        auto& p = std::get<PathValue>(val);

        std::vector<PS> nodes;
        std::vector<PS> rels;
        std::vector<PS> sequence;

        for (size_t i = 0; i < p.elements.size(); ++i) {
            const auto& elem = p.elements[i].value;
            if (std::holds_alternative<VertexValue>(elem)) {
                auto node_bolt = valueToBolt(elem, label_defs, edge_label_defs);
                nodes.push_back(PS{std::move(node_bolt)});
                sequence.push_back(PS{static_cast<int64_t>(nodes.size() - 1)});
            } else if (std::holds_alternative<EdgeValue>(elem)) {
                auto edge_bolt = valueToBolt(elem, label_defs, edge_label_defs);
                rels.push_back(PS{std::move(edge_bolt)});
                sequence.push_back(PS{static_cast<int64_t>(rels.size() - 1)});
            }
        }

        packstream::PackStreamStruct path_s;
        path_s.tag = tags::PATH;
        path_s.fields.push_back(PS{std::move(nodes)});
        path_s.fields.push_back(PS{std::move(rels)});
        path_s.fields.push_back(PS{std::move(sequence)});
        return path_s;
    } else if (std::holds_alternative<DateTimeValue>(val)) {
        return temporalToString(std::get<DateTimeValue>(val));
    } else if (std::holds_alternative<TimeValue>(val)) {
        return temporalToString(std::get<TimeValue>(val));
    } else if (std::holds_alternative<DurationValue>(val)) {
        return temporalToString(std::get<DurationValue>(val));
    } else if (std::holds_alternative<ListValue>(val)) {
        auto& lv = std::get<ListValue>(val);
        std::vector<PS> list;
        for (auto& elem : lv.elements)
            list.push_back(PS{valueToBolt(elem.value, label_defs, edge_label_defs)});
        return list;
    } else if (std::holds_alternative<MapValue>(val)) {
        auto& mv = std::get<MapValue>(val);
        std::unordered_map<std::string, PS> dict;
        for (auto& [key, elem] : mv.entries)
            dict[key] = PS{valueToBolt(elem.value, label_defs, edge_label_defs)};
        return dict;
    }

    return std::monostate{};
}

Value boltParamToValue(const packstream::Value& v) {
    if (std::holds_alternative<std::monostate>(v)) {
        return Value{};
    } else if (std::holds_alternative<bool>(v)) {
        return std::get<bool>(v);
    } else if (std::holds_alternative<int64_t>(v)) {
        return std::get<int64_t>(v);
    } else if (std::holds_alternative<double>(v)) {
        return std::get<double>(v);
    } else if (std::holds_alternative<std::string>(v)) {
        return std::get<std::string>(v);
    } else if (std::holds_alternative<std::vector<PS>>(v)) {
        ListValue lv;
        for (auto& elem : std::get<std::vector<PS>>(v)) {
            auto internal = boltParamToValue(elem.value);
            lv.elements.push_back({std::move(internal)});
        }
        return lv;
    } else if (std::holds_alternative<std::unordered_map<std::string, PS>>(v)) {
        MapValue mv;
        for (auto& [key, elem] : std::get<std::unordered_map<std::string, PS>>(v)) {
            auto internal = boltParamToValue(elem.value);
            mv.entries.push_back({key, ValueStorage{std::move(internal)}});
        }
        return mv;
    }
    return Value{};
}

} // namespace bolt
} // namespace eugraph
