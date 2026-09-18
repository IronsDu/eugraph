#include "service/bolt/bolt_value_mapping.hpp"

#include "common/types/graph_types.hpp"
#include "common/types/query_error.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "service/bolt/bolt_messages.hpp"

#include <limits>
#include <optional>
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
    } else if (std::holds_alternative<std::vector<uint8_t>>(pv)) {
        return std::get<std::vector<uint8_t>>(pv);
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
        if (boltMajorVersion(bolt_version) >= 5)
            node_s.fields.push_back(PS{std::to_string(static_cast<int64_t>(v.id))}); // element_id (Bolt v5.x)
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
        if (boltMajorVersion(bolt_version) >= 5) {
            rel_s.fields.push_back(PS{std::to_string(static_cast<int64_t>(e.id))});     // element_id (Bolt v5.x)
            rel_s.fields.push_back(PS{std::to_string(static_cast<int64_t>(e.src_id))}); // startNodeElementId
            rel_s.fields.push_back(PS{std::to_string(static_cast<int64_t>(e.dst_id))}); // endNodeElementId
        }
        return rel_s;
    } else if (std::holds_alternative<PathValue>(val)) {
        auto& p = std::get<PathValue>(val);

        std::vector<PS> nodes;
        std::vector<PS> rels;
        std::vector<PS> sequence;
        VertexId last_node_id = INVALID_VERTEX_ID;
        std::optional<int64_t> pending_rel;

        // Bolt PATH sequence is [rel_index, next_node_index, ...]. The first
        // element of a path is implicitly nodes[0]. A positive rel_index means
        // the relationship is traversed start->end; a negative index means
        // end->start.
        for (size_t i = 0; i < p.elements.size(); ++i) {
            const auto& elem = p.elements[i].value;
            if (std::holds_alternative<VertexValue>(elem)) {
                const auto& node = std::get<VertexValue>(elem);
                auto node_bolt = valueToBolt(elem, label_defs, edge_label_defs, bolt_version);
                nodes.push_back(PS{std::move(node_bolt)});
                if (pending_rel) {
                    sequence.push_back(PS{*pending_rel});
                    sequence.push_back(PS{static_cast<int64_t>(nodes.size() - 1)});
                    pending_rel.reset();
                }
                last_node_id = node.id;
            } else if (std::holds_alternative<EdgeValue>(elem)) {
                const auto& edge = std::get<EdgeValue>(elem);

                std::string type_name;
                auto elit = edge_label_defs.find(edge.label_id);
                if (elit != edge_label_defs.end())
                    type_name = elit->second.name;

                std::unordered_map<std::string, PS> props;
                if (edge.properties.has_value() && elit != edge_label_defs.end()) {
                    for (const auto& pd : elit->second.properties) {
                        if (pd.id < edge.properties->size()) {
                            const auto& pv = (*edge.properties)[pd.id];
                            if (pv.has_value())
                                props[pd.name] = PS{propertyToBolt(*pv, bolt_version)};
                        }
                    }
                }

                // PATH relationships use the unbound relationship signature:
                // id, type, properties and, in Bolt v5, element_id.
                packstream::PackStreamStruct rel_s;
                rel_s.tag = tags::UNBOUND_RELATIONSHIP;
                rel_s.fields.push_back(PS{static_cast<int64_t>(edge.id)});
                rel_s.fields.push_back(PS{std::move(type_name)});
                rel_s.fields.push_back(PS{std::move(props)});
                if (boltMajorVersion(bolt_version) >= 5)
                    rel_s.fields.push_back(PS{std::to_string(static_cast<int64_t>(edge.id))});
                rels.push_back(PS{std::move(rel_s)});
                int64_t rel_index = static_cast<int64_t>(rels.size());
                pending_rel = edge.src_id == last_node_id ? rel_index : -rel_index;
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
    } else if (std::holds_alternative<BytesValue>(val)) {
        return packstream::Value{std::get<BytesValue>(val).data};
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

namespace {

/// nanos-of-day → hour/minute/second/nanos
void fillTimeOfDayNanos(TimeValue& tv, int64_t nanos_of_day) {
    constexpr int64_t kNanosPerSecond = 1'000'000'000LL;
    tv.nanos = nanos_of_day % kNanosPerSecond;
    int64_t sec_of_day = nanos_of_day / kNanosPerSecond;
    tv.second = sec_of_day % 60;
    sec_of_day /= 60;
    tv.minute = sec_of_day % 60;
    tv.hour = sec_of_day / 60;
}

/// Bolt 时间结构体参数 → 内部时间值（BUG-10：以前参数里的时间类型直接落到 NULL）。
///
/// 结构体字段布局取自 Bolt 规范：
///   Date 'D'          {days}
///   Time 'T'          {nanoseconds, tz_offset_seconds}
///   LocalTime 't'     {nanoseconds}
///   DateTime 'I'/'F'  {seconds, nanoseconds, tz_offset_seconds}
///   DateTime 'i'/'f'  {seconds, nanoseconds, tz_id}
///   LocalDateTime 'd' {seconds, nanoseconds}
///   Duration 'E'      {months, days, seconds, nanoseconds}
std::optional<Value> temporalFromStruct(const packstream::PackStreamStruct& s) {
    auto as_int = [&s](size_t i) { return std::get<int64_t>(s.fields.at(i).value); };
    auto as_str = [&s](size_t i) { return std::get<std::string>(s.fields.at(i).value); };

    switch (s.tag) {
    case tags::DATE: {
        if (s.fields.size() != 1)
            return std::nullopt;
        DateTimeValue tv;
        tv.kind = DateTimeKind::DATE;
        civilFromDays(as_int(0), tv.year, tv.month, tv.day);
        return Value{tv};
    }
    case tags::LOCAL_TIME: {
        if (s.fields.size() != 1)
            return std::nullopt;
        TimeValue tv;
        tv.kind = TimeKind::LOCAL_TIME;
        fillTimeOfDayNanos(tv, as_int(0));
        return Value{tv};
    }
    case tags::TIME: {
        if (s.fields.size() != 2)
            return std::nullopt;
        TimeValue tv;
        tv.kind = TimeKind::TIME;
        fillTimeOfDayNanos(tv, as_int(0));
        tv.tz_offset_sec = static_cast<int32_t>(as_int(1));
        return Value{tv};
    }
    case tags::LOCAL_DATETIME: {
        if (s.fields.size() != 2)
            return std::nullopt;
        auto tv = datetimeFromEpoch(as_int(0), as_int(1));
        tv.kind = DateTimeKind::LOCAL_DATETIME;
        return Value{tv};
    }
    case tags::DATETIME:
    case tags::DATETIME_V4: {
        if (s.fields.size() != 3)
            return std::nullopt;
        const int32_t offset = static_cast<int32_t>(as_int(2));
        // 本地字段 = 绝对时刻 + 偏移；时区按偏移表示。用 128 位相加再判界：
        // 参数来自客户端，seconds 取 INT64_MAX 时直接相加是有符号溢出（UB）。
        const __int128 local_seconds = static_cast<__int128>(as_int(0)) + offset;
        if (local_seconds > std::numeric_limits<int64_t>::max() || local_seconds < std::numeric_limits<int64_t>::min())
            throw QueryException(QueryErrorKind::Argument, "datetime parameter out of range");
        auto tv = datetimeFromEpoch(static_cast<int64_t>(local_seconds), as_int(1));
        tv.kind = DateTimeKind::DATETIME;
        tv.tz_offset_sec = offset;
        return Value{tv};
    }
    case tags::DATETIME_ZONE_ID:
    case tags::DATETIME_ZONE_ID_V4: {
        if (s.fields.size() != 3)
            return std::nullopt;
        const std::string zone = as_str(2);
        auto utc = datetimeFromEpoch(as_int(0), as_int(1));
        // 命名时区的偏移取决于日期；先用 UTC 日期定位，再折算成该时区的本地字段。
        const int32_t offset = lookupNamedTimezoneOffset(utc.year, utc.month, utc.day, zone);
        const __int128 local_seconds = static_cast<__int128>(as_int(0)) + offset;
        if (local_seconds > std::numeric_limits<int64_t>::max() || local_seconds < std::numeric_limits<int64_t>::min())
            throw QueryException(QueryErrorKind::Argument, "datetime parameter out of range");
        auto tv = datetimeFromEpoch(static_cast<int64_t>(local_seconds), as_int(1));
        tv.kind = DateTimeKind::DATETIME;
        tv.tz_offset_sec = offset;
        tv.tz_name = zone;
        return Value{tv};
    }
    case tags::DURATION: {
        if (s.fields.size() != 4)
            return std::nullopt;
        DurationValue dv;
        dv.months = as_int(0);
        dv.days = as_int(1);
        dv.seconds = as_int(2);
        dv.nanos = as_int(3);
        normalizeDurationNanos(dv);
        return Value{dv};
    }
    default:
        return std::nullopt;
    }
}

} // namespace

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
    } else if (std::holds_alternative<std::vector<uint8_t>>(v)) {
        return BytesValue{std::get<std::vector<uint8_t>>(v)};
    } else if (std::holds_alternative<packstream::PackStreamStruct>(v)) {
        const auto& s = std::get<packstream::PackStreamStruct>(v);
        if (auto temporal = temporalFromStruct(s))
            return *temporal;
        return Value{};
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
