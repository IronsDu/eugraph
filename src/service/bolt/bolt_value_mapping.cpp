#include "service/bolt/bolt_value_mapping.hpp"

#include "service/bolt/bolt_messages.hpp"
#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"

#include <string>

namespace eugraph {
namespace service {
namespace bolt {

using PS = packstream::PackStreamValueStorage;

namespace {

inline uint8_t boltMajorVersion(uint32_t ver) {
    return static_cast<uint8_t>((ver >> 8) & 0xFF);
}

packstream::PackStreamStruct dateTimeToStruct(const DateTimeValue& tv, uint32_t bolt_version) {
    int64_t days = daysFromCivil(tv.year, tv.month, tv.day);
    packstream::PackStreamStruct s;
    switch (tv.kind) {
    case DateTimeKind::DATE:
        s.tag = tags::DATE;
        s.fields.push_back(PS{days});
        break;
    case DateTimeKind::LOCAL_DATETIME: {
        int64_t epoch_seconds = days * 86400 + tv.hour * 3600 + tv.minute * 60 + tv.second;
        s.tag = tags::LOCAL_DATETIME;
        s.fields.push_back(PS{epoch_seconds});
        s.fields.push_back(PS{tv.nanos});
        break;
    }
    case DateTimeKind::DATETIME: {
        int64_t local_seconds = days * 86400 + tv.hour * 3600 + tv.minute * 60 + tv.second;
        bool is_v4 = boltMajorVersion(bolt_version) <= 4;
        if (is_v4) {
            // v4.x: local wall-clock seconds, tags 0x46/0x66
            if (!tv.tz_name.empty()) {
                s.tag = tags::DATETIME_ZONE_ID_V4;
                s.fields.push_back(PS{local_seconds});
                s.fields.push_back(PS{tv.nanos});
                s.fields.push_back(PS{std::string{tv.tz_name}});
            } else {
                s.tag = tags::DATETIME_V4;
                s.fields.push_back(PS{local_seconds});
                s.fields.push_back(PS{tv.nanos});
                s.fields.push_back(PS{static_cast<int64_t>(tv.tz_offset_sec)});
            }
        } else {
            // v5.x: UTC epoch seconds, tags 0x49/0x69
            int64_t utc_seconds = local_seconds - tv.tz_offset_sec;
            if (!tv.tz_name.empty()) {
                s.tag = tags::DATETIME_ZONE_ID;
                s.fields.push_back(PS{utc_seconds});
                s.fields.push_back(PS{tv.nanos});
                s.fields.push_back(PS{std::string{tv.tz_name}});
            } else {
                s.tag = tags::DATETIME;
                s.fields.push_back(PS{utc_seconds});
                s.fields.push_back(PS{tv.nanos});
                s.fields.push_back(PS{static_cast<int64_t>(tv.tz_offset_sec)});
            }
        }
        break;
    }
    }
    return s;
}

packstream::PackStreamStruct timeToStruct(const TimeValue& tv) {
    int64_t nanos_of_day = (tv.hour * 3600 + tv.minute * 60 + tv.second) * 1000000000LL + tv.nanos;
    packstream::PackStreamStruct s;
    switch (tv.kind) {
    case TimeKind::LOCAL_TIME:
        s.tag = tags::LOCAL_TIME;
        s.fields.push_back(PS{nanos_of_day});
        break;
    case TimeKind::TIME:
        s.tag = tags::TIME;
        s.fields.push_back(PS{nanos_of_day});
        s.fields.push_back(PS{static_cast<int64_t>(tv.tz_offset_sec)});
        break;
    }
    return s;
}

packstream::PackStreamStruct durationToStruct(const DurationValue& dv) {
    packstream::PackStreamStruct s;
    s.tag = tags::DURATION;
    s.fields.push_back(PS{dv.months});
    s.fields.push_back(PS{dv.days});
    s.fields.push_back(PS{dv.seconds});
    s.fields.push_back(PS{dv.nanos});
    return s;
}

packstream::Value propertyToBolt(const PropertyValue& pv, uint32_t bolt_version) {
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
        return dateTimeToStruct(std::get<DateTimeValue>(pv), bolt_version);
    } else if (std::holds_alternative<TimeValue>(pv)) {
        return timeToStruct(std::get<TimeValue>(pv));
    } else if (std::holds_alternative<DurationValue>(pv)) {
        return durationToStruct(std::get<DurationValue>(pv));
    } else if (std::holds_alternative<std::vector<DateTimeValue>>(pv)) {
        std::vector<PS> list;
        for (auto& tv : std::get<std::vector<DateTimeValue>>(pv))
            list.push_back(PS{dateTimeToStruct(tv, bolt_version)});
        return list;
    } else if (std::holds_alternative<std::vector<TimeValue>>(pv)) {
        std::vector<PS> list;
        for (auto& tv : std::get<std::vector<TimeValue>>(pv))
            list.push_back(PS{timeToStruct(tv)});
        return list;
    } else if (std::holds_alternative<std::vector<DurationValue>>(pv)) {
        std::vector<PS> list;
        for (auto& dv : std::get<std::vector<DurationValue>>(pv))
            list.push_back(PS{durationToStruct(dv)});
        return list;
    }
    return std::monostate{};
}

} // anonymous namespace

packstream::Value valueToBolt(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                              const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs,
                              uint32_t bolt_version) {
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
                        props[pd.name] = PS{propertyToBolt(*pv, bolt_version)};
                }
            }
        }

        packstream::PackStreamStruct node_s;
        node_s.tag = tags::NODE;
        node_s.fields.push_back(PS{static_cast<int64_t>(v.id)});
        node_s.fields.push_back(PS{std::move(label_list)});
        node_s.fields.push_back(PS{std::move(props)});
        node_s.fields.push_back(PS{std::to_string(v.id)}); // element_id (Bolt v5.1)
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
                        props[pd.name] = PS{propertyToBolt(*pv, bolt_version)};
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
        rel_s.fields.push_back(PS{std::to_string(e.id)});     // element_id (Bolt v5.1)
        rel_s.fields.push_back(PS{std::to_string(e.src_id)}); // startNodeElementId (Bolt v5.1)
        rel_s.fields.push_back(PS{std::to_string(e.dst_id)}); // endNodeElementId (Bolt v5.1)
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
        return dateTimeToStruct(std::get<DateTimeValue>(val), bolt_version);
    } else if (std::holds_alternative<TimeValue>(val)) {
        return timeToStruct(std::get<TimeValue>(val));
    } else if (std::holds_alternative<DurationValue>(val)) {
        return durationToStruct(std::get<DurationValue>(val));
    } else if (std::holds_alternative<ListValue>(val)) {
        auto& lv = std::get<ListValue>(val);
        std::vector<PS> list;
        for (auto& elem : lv.elements)
            list.push_back(PS{valueToBolt(elem.value, label_defs, edge_label_defs, bolt_version)});
        return list;
    } else if (std::holds_alternative<MapValue>(val)) {
        auto& mv = std::get<MapValue>(val);
        std::unordered_map<std::string, PS> dict;
        for (auto& [key, elem] : mv.entries)
            dict[key] = PS{valueToBolt(elem.value, label_defs, edge_label_defs, bolt_version)};
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
} // namespace service
} // namespace eugraph
