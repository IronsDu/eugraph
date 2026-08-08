#include "query/physical_plan/operator/call_physical_op.hpp"

#include "common/types/graph_types.hpp"

#include <spdlog/spdlog.h>

#include <string>

namespace eugraph {
namespace compute {

namespace {

const char* propTypeName(PropertyType t) {
    switch (t) {
    case PropertyType::BOOL:
        return "BOOLEAN";
    case PropertyType::INT64:
        return "INTEGER";
    case PropertyType::DOUBLE:
        return "FLOAT";
    case PropertyType::STRING:
        return "STRING";
    case PropertyType::INT64_ARRAY:
        return "INTEGER_ARRAY";
    case PropertyType::DOUBLE_ARRAY:
        return "FLOAT_ARRAY";
    case PropertyType::STRING_ARRAY:
        return "STRING_ARRAY";
    case PropertyType::DATETIME:
        return "DATE_TIME";
    case PropertyType::TIME:
        return "TIME";
    case PropertyType::DURATION:
        return "DURATION";
    case PropertyType::DATETIME_ARRAY:
        return "DATE_TIME_ARRAY";
    case PropertyType::TIME_ARRAY:
        return "TIME_ARRAY";
    case PropertyType::DURATION_ARRAY:
        return "DURATION_ARRAY";
    default:
        return "ANY";
    }
}

/// Build a MapValue representing one node label entry for db.schema.visualization.
MapValue buildNodeEntry(const LabelDef& ldef) {
    MapValue entry;

    entry.entries.push_back({"name", ValueStorage{ldef.name}});

    ListValue labels;
    labels.elements.push_back({ValueStorage{ldef.name}});
    entry.entries.push_back({"labels", ValueStorage{Value{labels}}});

    MapValue props;
    for (const auto& pd : ldef.properties) {
        MapValue prop_info;
        prop_info.entries.push_back({"type", ValueStorage{std::string{propTypeName(pd.type)}}});
        props.entries.push_back({pd.name, ValueStorage{Value{prop_info}}});
    }
    entry.entries.push_back({"properties", ValueStorage{Value{props}}});

    ListValue indexes;
    for (const auto& idx : ldef.indexes) {
        MapValue idx_info;
        idx_info.entries.push_back({"name", ValueStorage{idx.name}});
        ListValue idx_props;
        for (auto prop_id : idx.prop_ids) {
            for (const auto& pd : ldef.properties) {
                if (pd.id == prop_id)
                    idx_props.elements.push_back({ValueStorage{pd.name}});
            }
        }
        idx_info.entries.push_back({"properties", ValueStorage{Value{idx_props}}});
        idx_info.entries.push_back({"unique", ValueStorage{idx.unique}});
        indexes.elements.push_back({ValueStorage{Value{idx_info}}});
    }
    entry.entries.push_back({"indexes", ValueStorage{Value{indexes}}});

    entry.entries.push_back({"constraints", ValueStorage{Value{ListValue{}}}});

    return entry;
}

MapValue buildEdgeEntry(const EdgeLabelDef& eldef) {
    MapValue entry;

    entry.entries.push_back({"name", ValueStorage{eldef.name}});
    entry.entries.push_back({"relationshipType", ValueStorage{eldef.name}});

    MapValue props;
    for (const auto& pd : eldef.properties) {
        MapValue prop_info;
        prop_info.entries.push_back({"type", ValueStorage{std::string{propTypeName(pd.type)}}});
        props.entries.push_back({pd.name, ValueStorage{Value{prop_info}}});
    }
    entry.entries.push_back({"properties", ValueStorage{Value{props}}});

    ListValue indexes;
    for (const auto& idx : eldef.indexes) {
        MapValue idx_info;
        idx_info.entries.push_back({"name", ValueStorage{idx.name}});
        ListValue idx_props;
        for (auto prop_id : idx.prop_ids) {
            for (const auto& pd : eldef.properties) {
                if (pd.id == prop_id)
                    idx_props.elements.push_back({ValueStorage{pd.name}});
            }
        }
        idx_info.entries.push_back({"properties", ValueStorage{Value{idx_props}}});
        idx_info.entries.push_back({"unique", ValueStorage{idx.unique}});
        indexes.elements.push_back({ValueStorage{Value{idx_info}}});
    }
    entry.entries.push_back({"indexes", ValueStorage{Value{indexes}}});

    entry.entries.push_back({"constraints", ValueStorage{Value{ListValue{}}}});

    return entry;
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> CallPhysicalOp::executeChunk() {
    spdlog::info("[CallPhysicalOp] executeChunk called, procedure={}", procedure_name_);
    DataChunk output;
    output.setSchema(output_types_);
    output.reserve(1);
    output.count = 1;

    if (procedure_name_ == "db.ping") {
        spdlog::info("[CallPhysicalOp] db.ping path");
        output.columns[0].setValue(0, Value(true));
    } else if (procedure_name_ == "db.schema.visualization") {
        spdlog::info("[CallPhysicalOp] db.schema.visualization path, meta_={}", (void*)meta_);
        ListValue nodes;
        ListValue rels;

        if (meta_) {
            auto labels = co_await meta_->listLabels();
            for (const auto& ldef : labels) {
                if (ldef.name == kAnonLabelName)
                    continue;
                nodes.elements.push_back({ValueStorage{Value{buildNodeEntry(ldef)}}});
            }

            auto edge_labels = co_await meta_->listEdgeLabels();
            for (const auto& eldef : edge_labels)
                rels.elements.push_back({ValueStorage{Value{buildEdgeEntry(eldef)}}});
        }

        spdlog::info("[CallPhysicalOp] built schema: {} nodes, {} rels", nodes.elements.size(), rels.elements.size());
        output.columns[0].setValue(0, Value{nodes});
        output.columns[1].setValue(0, Value{rels});
    }

    spdlog::info("[CallPhysicalOp] yielding output with count={}", output.count);
    co_yield output;
}

} // namespace compute
} // namespace eugraph
