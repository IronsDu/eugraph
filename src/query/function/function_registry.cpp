#include "query/function/function_registry.hpp"

#include "query/dataset/data_chunk.hpp"

#include "query/function/aggregate/avg_function.hpp"
#include "query/function/aggregate/collect_function.hpp"
#include "query/function/aggregate/count_function.hpp"
#include "query/function/aggregate/min_function.hpp"
#include "query/function/aggregate/min_function.hpp" // also provides MaxState
#include "query/function/aggregate/percentile_function.hpp"
#include "query/function/aggregate/sum_function.hpp"
#include "query/function/scalar/coalesce_function.hpp"
#include "query/function/scalar/conversion_functions.hpp"
#include "query/function/scalar/graph_functions.hpp"
#include "query/function/scalar/id_function.hpp"
#include "query/function/scalar/list_functions.hpp"
#include "query/function/scalar/math_functions.hpp"
#include "query/function/scalar/path_functions.hpp"
#include "query/function/scalar/start_end_node_function.hpp"
#include "query/function/scalar/string_functions.hpp"
#include "query/function/scalar/temporal_functions.hpp"
#include "query/function/scalar/type_function.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace eugraph {
namespace function {

FunctionRegistry::FunctionRegistry() {
    registerBuiltins();
}

void FunctionRegistry::registerBuiltins() {
    registerScalarBuiltins();
    registerAggregateBuiltins();
}

namespace {

function::FunctionFn scalarToBatch(function::ScalarFn scalar) {
    return [scalar = std::move(scalar)](const std::vector<const Column*>& args, Column& result, size_t count,
                                        const function::EvalContext& ctx) {
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            std::vector<Value> values;
            values.reserve(args.size());
            for (const auto* col : args) {
                values.push_back(col ? col->getValue(i) : Value{});
            }
            result.setValue(i, scalar(values, ctx));
        }
    };
}

void edgeUniqueBatch(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext& ctx);
} // namespace

void FunctionRegistry::registerScalarBuiltins() {
    using binder::BoundType;

    // id(Vertex) -> Int64
    functions_["id"].push_back(
        {"id", {BoundType::Vertex()}, BoundType::Int64(), false, false, scalar::idBatchFn, {}, {}, {}});
    // id(VertexRef) -> Int64
    functions_["id"].push_back(
        {"id", {BoundType::VertexRef()}, BoundType::Int64(), false, false, scalar::idBatchFn, {}, {}, {}});
    // id(Edge) -> Int64
    functions_["id"].push_back(
        {"id", {BoundType::Edge()}, BoundType::Int64(), false, false, scalar::idBatchFn, {}, {}, {}});
    // id(EdgeKey) -> Int64
    functions_["id"].push_back(
        {"id", {BoundType::EdgeKey()}, BoundType::Int64(), false, false, scalar::idBatchFn, {}, {}, {}});

    // elementId(Vertex|VertexRef|Edge|EdgeKey) -> String
    // 注册名与 neo4j 一致（驼峰）；element_id 作为历史别名保留。
    for (const char* name : {"elementId", "element_id"}) {
        functions_[name].push_back(
            {name, {BoundType::Vertex()}, BoundType::String(), false, false, scalar::elementIdBatchFn, {}, {}, {}});
        functions_[name].push_back(
            {name, {BoundType::VertexRef()}, BoundType::String(), false, false, scalar::elementIdBatchFn, {}, {}, {}});
        functions_[name].push_back(
            {name, {BoundType::Edge()}, BoundType::String(), false, false, scalar::elementIdBatchFn, {}, {}, {}});
        functions_[name].push_back(
            {name, {BoundType::EdgeKey()}, BoundType::String(), false, false, scalar::elementIdBatchFn, {}, {}, {}});
    }

    // abs(Int64) -> Int64
    functions_["abs"].push_back(
        {"abs", {BoundType::Int64()}, BoundType::Int64(), false, false, scalar::absBatchFn, {}, {}, {}});
    // abs(Double) -> Double
    functions_["abs"].push_back(
        {"abs", {BoundType::Double()}, BoundType::Double(), false, false, scalar::absBatchFn, {}, {}, {}});

    // ceil(Int64) -> Double
    functions_["ceil"].push_back(
        {"ceil", {BoundType::Int64()}, BoundType::Double(), false, false, scalar::ceilBatchFn, {}, {}, {}});
    // ceil(Double) -> Double
    functions_["ceil"].push_back(
        {"ceil", {BoundType::Double()}, BoundType::Double(), false, false, scalar::ceilBatchFn, {}, {}, {}});

    // floor(Int64) -> Double
    functions_["floor"].push_back(
        {"floor", {BoundType::Int64()}, BoundType::Double(), false, false, scalar::floorBatchFn, {}, {}, {}});
    // floor(Double) -> Double
    functions_["floor"].push_back(
        {"floor", {BoundType::Double()}, BoundType::Double(), false, false, scalar::floorBatchFn, {}, {}, {}});

    // sign(Int64) -> Int64
    functions_["sign"].push_back(
        {"sign", {BoundType::Int64()}, BoundType::Int64(), false, false, scalar::signBatchFn, {}, {}, {}});
    // sign(Double) -> Int64 (Cypher: returns -1/0/1 as integer)
    functions_["sign"].push_back(
        {"sign", {BoundType::Double()}, BoundType::Int64(), false, false, scalar::signBatchFn, {}, {}, {}});

    // nodes(Path) -> List<Vertex>
    functions_["nodes"].push_back({"nodes",
                                   {BoundType::Path()},
                                   BoundType::List(BoundType::Vertex()),
                                   false,
                                   false,
                                   scalar::nodesBatchFn,
                                   {},
                                   {},
                                   {}});

    // relationships(Path) -> List<Edge>
    functions_["relationships"].push_back({"relationships",
                                           {BoundType::Path()},
                                           BoundType::List(BoundType::Edge()),
                                           false,
                                           false,
                                           scalar::relationshipsBatchFn,
                                           {},
                                           {},
                                           {}});

    // length(Path) -> Int64
    functions_["length"].push_back(
        {"length", {BoundType::Path()}, BoundType::Int64(), false, false, scalar::lengthBatchFn, {}, {}, {}});

    // type(Edge) -> String
    functions_["type"].push_back(
        {"type", {BoundType::Edge()}, BoundType::String(), false, false, scalar::typeBatchFn, {}, {}, {}});

    // startNode(Edge) -> Vertex
    functions_["startNode"].push_back(
        {"startNode", {BoundType::Edge()}, BoundType::Vertex(), false, false, scalar::startNodeBatchFn, {}, {}, {}});

    // endNode(Edge) -> Vertex
    functions_["endNode"].push_back(
        {"endNode", {BoundType::Edge()}, BoundType::Vertex(), false, false, scalar::endNodeBatchFn, {}, {}, {}});

    // last(List<Any>) -> Any
    functions_["last"].push_back(
        {"last", {BoundType::List(BoundType::Any())}, BoundType::Any(), false, false, scalar::lastBatchFn, {}, {}, {}});

    // head(List<Any>) -> Any
    functions_["head"].push_back(
        {"head", {BoundType::List(BoundType::Any())}, BoundType::Any(), false, false, scalar::headBatchFn, {}, {}, {}});

    // tail(List<Any>) -> List<Any>
    functions_["tail"].push_back({"tail",
                                  {BoundType::List(BoundType::Any())},
                                  BoundType::List(BoundType::Any()),
                                  false,
                                  false,
                                  scalar::tailBatchFn,
                                  {},
                                  {},
                                  {}});

    // reverse(List<Any>) -> List<Any>
    functions_["reverse"].push_back({"reverse",
                                     {BoundType::List(BoundType::Any())},
                                     BoundType::List(BoundType::Any()),
                                     false,
                                     false,
                                     scalar::reverseBatchFn,
                                     {},
                                     {},
                                     {}});

    // reverse(String) -> String
    functions_["reverse"].push_back({"reverse",
                                     {BoundType::String()},
                                     BoundType::String(),
                                     false,
                                     false,
                                     scalar::reverseStringBatchFn,
                                     {},
                                     {},
                                     {}});

    // size(List<Any>) -> Int64
    functions_["size"].push_back({"size",
                                  {BoundType::List(BoundType::Any())},
                                  BoundType::Int64(),
                                  false,
                                  false,
                                  scalar::sizeListBatchFn,
                                  {},
                                  {},
                                  {}});

    // size(String) -> Int64
    functions_["size"].push_back(
        {"size", {BoundType::String()}, BoundType::Int64(), false, false, scalar::sizeStringBatchFn, {}, {}, {}});

    // sqrt(Double) -> Double
    functions_["sqrt"].push_back(
        {"sqrt", {BoundType::Double()}, BoundType::Double(), false, false, scalar::sqrtBatchFn, {}, {}, {}});

    // rand() -> Double
    functions_["rand"].push_back({"rand", {}, BoundType::Double(), false, false, scalar::randBatchFn, {}, {}, {}});

    // range(Any, Any) -> List<Int64>
    // Cypher defers argument type validation to runtime (ArgumentError /
    // InvalidArgumentType). Accept ANY at bind time; the scalar fn throws.
    functions_["range"].push_back({"range",
                                   {BoundType::Any(), BoundType::Any()},
                                   BoundType::List(BoundType::Int64()),
                                   false,
                                   false,
                                   scalar::rangeBatchFn,
                                   {},
                                   {},
                                   {}});

    // range(Any, Any, Any) -> List<Int64>
    functions_["range"].push_back({"range",
                                   {BoundType::Any(), BoundType::Any(), BoundType::Any()},
                                   BoundType::List(BoundType::Int64()),
                                   false,
                                   false,
                                   scalar::rangeBatchFn,
                                   {},
                                   {},
                                   {}});

    // toInteger(Any) -> Int64
    functions_["toInteger"].push_back(
        {"toInteger", {}, BoundType::Int64(), false, true, scalar::toIntegerBatchFn, {}, {}, {}});

    // toFloat(Any) -> Double
    functions_["toFloat"].push_back(
        {"toFloat", {}, BoundType::Double(), false, true, scalar::toFloatBatchFn, {}, {}, {}});

    // toBoolean(Any) -> Boolean
    functions_["toBoolean"].push_back(
        {"toBoolean", {}, BoundType::Bool(), false, true, scalar::toBooleanBatchFn, {}, {}, {}});

    // toString(Any) -> String
    functions_["toString"].push_back(
        {"toString", {BoundType::Any()}, BoundType::String(), false, false, scalar::toStringBatchFn, {}, {}, {}});

    // labels(Vertex) -> List<String>
    functions_["labels"].push_back({"labels",
                                    {BoundType::Vertex()},
                                    BoundType::List(BoundType::String()),
                                    false,
                                    false,
                                    scalar::labelsBatchFn,
                                    {},
                                    {},
                                    {}});

    // labels(Edge) -> List<String>
    functions_["labels"].push_back({"labels",
                                    {BoundType::Edge()},
                                    BoundType::List(BoundType::String()),
                                    false,
                                    false,
                                    scalar::labelsBatchFn,
                                    {},
                                    {},
                                    {}});

    // keys(Vertex) -> List<String>
    functions_["keys"].push_back({"keys",
                                  {BoundType::Vertex()},
                                  BoundType::List(BoundType::String()),
                                  false,
                                  false,
                                  scalar::keysBatchFn,
                                  {},
                                  {},
                                  {}});

    // keys(Edge) -> List<String>
    functions_["keys"].push_back({"keys",
                                  {BoundType::Edge()},
                                  BoundType::List(BoundType::String()),
                                  false,
                                  false,
                                  scalar::keysBatchFn,
                                  {},
                                  {},
                                  {}});

    // keys(Map) -> List<String>
    functions_["keys"].push_back({"keys",
                                  {BoundType::Map(BoundType::String(), BoundType::Any())},
                                  BoundType::List(BoundType::String()),
                                  false,
                                  false,
                                  scalar::keysBatchFn,
                                  {},
                                  {},
                                  {}});

    // properties(Vertex) -> Map<String, Any>
    functions_["properties"].push_back({"properties",
                                        {BoundType::Vertex()},
                                        BoundType::Map(BoundType::String(), BoundType::Any()),
                                        false,
                                        false,
                                        scalar::propertiesBatchFn,
                                        {},
                                        {},
                                        {}});

    // properties(Edge) -> Map<String, Any>
    functions_["properties"].push_back({"properties",
                                        {BoundType::Edge()},
                                        BoundType::Map(BoundType::String(), BoundType::Any()),
                                        false,
                                        false,
                                        scalar::propertiesBatchFn,
                                        {},
                                        {},
                                        {}});

    // properties(Map<String, Any>) -> Map<String, Any> (identity)
    functions_["properties"].push_back({"properties",
                                        {BoundType::Map(BoundType::String(), BoundType::Any())},
                                        BoundType::Map(BoundType::String(), BoundType::Any()),
                                        false,
                                        false,
                                        scalar::propertiesBatchFn,
                                        {},
                                        {},
                                        {}});

    // --- String functions ---

    // trim(String) -> String
    functions_["trim"].push_back(
        {"trim", {BoundType::String()}, BoundType::String(), false, false, scalar::trimBatchFn, {}, {}, {}});
    // trim(String, String) -> String：去掉两端/单端出现的任意 trim 字符
    functions_["trim"].push_back({"trim",
                                  {BoundType::String(), BoundType::String()},
                                  BoundType::String(),
                                  false,
                                  false,
                                  scalar::trimBatchFn,
                                  {},
                                  {},
                                  {}});

    // ltrim(String) -> String
    functions_["ltrim"].push_back(
        {"ltrim", {BoundType::String()}, BoundType::String(), false, false, scalar::ltrimBatchFn, {}, {}, {}});
    // ltrim(String, String) -> String：去掉两端/单端出现的任意 trim 字符
    functions_["ltrim"].push_back({"ltrim",
                                   {BoundType::String(), BoundType::String()},
                                   BoundType::String(),
                                   false,
                                   false,
                                   scalar::ltrimBatchFn,
                                   {},
                                   {},
                                   {}});

    // rtrim(String) -> String
    functions_["rtrim"].push_back(
        {"rtrim", {BoundType::String()}, BoundType::String(), false, false, scalar::rtrimBatchFn, {}, {}, {}});
    // rtrim(String, String) -> String：去掉两端/单端出现的任意 trim 字符
    functions_["rtrim"].push_back({"rtrim",
                                   {BoundType::String(), BoundType::String()},
                                   BoundType::String(),
                                   false,
                                   false,
                                   scalar::rtrimBatchFn,
                                   {},
                                   {},
                                   {}});

    // split(String, String) -> List<String>
    functions_["split"].push_back({"split",
                                   {BoundType::String(), BoundType::String()},
                                   BoundType::List(BoundType::String()),
                                   false,
                                   false,
                                   scalar::splitBatchFn,
                                   {},
                                   {},
                                   {}});

    // replace(String, String, String) -> String
    functions_["replace"].push_back({"replace",
                                     {BoundType::String(), BoundType::String(), BoundType::String()},
                                     BoundType::String(),
                                     false,
                                     false,
                                     scalar::replaceBatchFn,
                                     {},
                                     {},
                                     {}});

    // substring(String, Int64) -> String
    functions_["substring"].push_back({"substring",
                                       {BoundType::String(), BoundType::Int64()},
                                       BoundType::String(),
                                       false,
                                       false,
                                       scalar::substringBatchFn,
                                       {},
                                       {},
                                       {}});

    // substring(String, Int64, Int64) -> String
    functions_["substring"].push_back({"substring",
                                       {BoundType::String(), BoundType::Int64(), BoundType::Int64()},
                                       BoundType::String(),
                                       false,
                                       false,
                                       scalar::substringBatchFn,
                                       {},
                                       {},
                                       {}});

    // left(String, Int64) -> String
    functions_["left"].push_back({"left",
                                  {BoundType::String(), BoundType::Int64()},
                                  BoundType::String(),
                                  false,
                                  false,
                                  scalar::leftBatchFn,
                                  {},
                                  {},
                                  {}});

    // right(String, Int64) -> String
    functions_["right"].push_back({"right",
                                   {BoundType::String(), BoundType::Int64()},
                                   BoundType::String(),
                                   false,
                                   false,
                                   scalar::rightBatchFn,
                                   {},
                                   {},
                                   {}});

    // toUpper(String) -> String
    functions_["toUpper"].push_back(
        {"toUpper", {BoundType::String()}, BoundType::String(), false, false, scalar::toUpperBatchFn, {}, {}, {}});

    // toLower(String) -> String
    functions_["toLower"].push_back(
        {"toLower", {BoundType::String()}, BoundType::String(), false, false, scalar::toLowerBatchFn, {}, {}, {}});

    // --- Coalesce ---

    // coalesce(Any...) -> Any
    functions_["coalesce"].push_back(
        {"coalesce", {}, BoundType::Any(), false, true, scalar::coalesceBatchFn, {}, {}, {}});

    // --- Temporal constructors ---

    // date() -> DateTime (0-arg: epoch 1970-01-01)
    functions_["date"].push_back({"date", {}, BoundType::DateTime(), false, true, scalar::dateBatchFn, {}, {}, {}});
    // date(MAP<STRING, ANY>) -> DateTime
    functions_["date"].push_back({"date",
                                  {BoundType::Map(BoundType::String(), BoundType::Any())},
                                  BoundType::DateTime(),
                                  false,
                                  false,
                                  scalar::dateBatchFn,
                                  {},
                                  {},
                                  {}});
    // date(STRING) -> DateTime
    functions_["date"].push_back(
        {"date", {BoundType::String()}, BoundType::DateTime(), false, false, scalar::dateBatchFn, {}, {}, {}});

    // localtime() -> Time (0-arg: epoch 00:00:00)
    functions_["localtime"].push_back(
        {"localtime", {}, BoundType::Time(), false, true, scalar::localtimeBatchFn, {}, {}, {}});
    // localtime(MAP<STRING, ANY>) -> Time
    functions_["localtime"].push_back({"localtime",
                                       {BoundType::Map(BoundType::String(), BoundType::Any())},
                                       BoundType::Time(),
                                       false,
                                       false,
                                       scalar::localtimeBatchFn,
                                       {},
                                       {},
                                       {}});
    // localtime(STRING) -> Time
    functions_["localtime"].push_back(
        {"localtime", {BoundType::String()}, BoundType::Time(), false, false, scalar::localtimeBatchFn, {}, {}, {}});

    // time() -> Time (0-arg: epoch 00:00:00Z)
    functions_["time"].push_back({"time", {}, BoundType::Time(), false, true, scalar::timeBatchFn, {}, {}, {}});
    // time(MAP<STRING, ANY>) -> Time
    functions_["time"].push_back({"time",
                                  {BoundType::Map(BoundType::String(), BoundType::Any())},
                                  BoundType::Time(),
                                  false,
                                  false,
                                  scalar::timeBatchFn,
                                  {},
                                  {},
                                  {}});
    // time(STRING) -> Time
    functions_["time"].push_back(
        {"time", {BoundType::String()}, BoundType::Time(), false, false, scalar::timeBatchFn, {}, {}, {}});

    // localdatetime() -> DateTime (0-arg: epoch 1970-01-01T00:00:00)
    functions_["localdatetime"].push_back(
        {"localdatetime", {}, BoundType::DateTime(), false, true, scalar::localdatetimeBatchFn, {}, {}, {}});
    // localdatetime(MAP<STRING, ANY>) -> DateTime
    functions_["localdatetime"].push_back({"localdatetime",
                                           {BoundType::Map(BoundType::String(), BoundType::Any())},
                                           BoundType::DateTime(),
                                           false,
                                           false,
                                           scalar::localdatetimeBatchFn,
                                           {},
                                           {},
                                           {}});
    // localdatetime(STRING) -> DateTime
    functions_["localdatetime"].push_back({"localdatetime",
                                           {BoundType::String()},
                                           BoundType::DateTime(),
                                           false,
                                           false,
                                           scalar::localdatetimeBatchFn,
                                           {},
                                           {},
                                           {}});

    // datetime() -> DateTime (0-arg: epoch 1970-01-01T00:00:00Z)
    functions_["datetime"].push_back(
        {"datetime", {}, BoundType::DateTime(), false, true, scalar::datetimeBatchFn, {}, {}, {}});
    // datetime(MAP<STRING, ANY>) -> DateTime
    functions_["datetime"].push_back({"datetime",
                                      {BoundType::Map(BoundType::String(), BoundType::Any())},
                                      BoundType::DateTime(),
                                      false,
                                      false,
                                      scalar::datetimeBatchFn,
                                      {},
                                      {},
                                      {}});
    // datetime(STRING) -> DateTime
    functions_["datetime"].push_back(
        {"datetime", {BoundType::String()}, BoundType::DateTime(), false, false, scalar::datetimeBatchFn, {}, {}, {}});

    // duration(MAP<STRING, ANY>) -> Duration
    functions_["duration"].push_back({"duration",
                                      {BoundType::Map(BoundType::String(), BoundType::Any())},
                                      BoundType::Duration(),
                                      false,
                                      false,
                                      scalar::durationBatchFn,
                                      {},
                                      {},
                                      {}});
    // duration(STRING) -> Duration
    functions_["duration"].push_back(
        {"duration", {BoundType::String()}, BoundType::Duration(), false, false, scalar::durationBatchFn, {}, {}, {}});

    // __temporal_field__(ANY, INT64) -> Any
    functions_["__temporal_field__"].push_back({"__temporal_field__",
                                                {BoundType::Any(), BoundType::Int64()},
                                                BoundType::Any(),
                                                false,
                                                false,
                                                scalar::temporalAccessorBatchFn,
                                                {},
                                                {},
                                                {}});
    // __temporal_field__(STRING, INT64) -> Any  (for property round-trip)
    functions_["__temporal_field__"].push_back({"__temporal_field__",
                                                {BoundType::String(), BoundType::Int64()},
                                                BoundType::Any(),
                                                false,
                                                false,
                                                scalar::temporalAccessorBatchFn,
                                                {},
                                                {},
                                                {}});
    // __temporal_field__(ANY, INT64) -> Any  (for dynamic property round-trip)
    functions_["__temporal_field__"].push_back({"__temporal_field__",
                                                {BoundType::Any(), BoundType::Int64()},
                                                BoundType::Any(),
                                                false,
                                                false,
                                                scalar::temporalAccessorBatchFn,
                                                {},
                                                {},
                                                {}});

    // truncate(STRING, DATETIME) -> DateTime  (2-arg)
    // truncate(STRING, DATETIME, ANY) -> DateTime  (3-arg)
    functions_["date.truncate"].push_back(
        {"date.truncate",
         {BoundType::String(), BoundType::DateTime()},
         BoundType::DateTime(),
         false,
         false,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(DateTimeKind::DATE), false>,
         {},
         {},
         {}});
    functions_["date.truncate"].push_back(
        {"date.truncate",
         {BoundType::String(), BoundType::DateTime(), BoundType::Any()},
         BoundType::DateTime(),
         false,
         false,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(DateTimeKind::DATE), false>,
         {},
         {},
         {}});
    functions_["datetime.truncate"].push_back(
        {"datetime.truncate",
         {BoundType::String(), BoundType::DateTime()},
         BoundType::DateTime(),
         false,
         false,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(DateTimeKind::DATETIME), false>,
         {},
         {},
         {}});
    functions_["datetime.truncate"].push_back(
        {"datetime.truncate",
         {BoundType::String(), BoundType::DateTime(), BoundType::Any()},
         BoundType::DateTime(),
         false,
         false,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(DateTimeKind::DATETIME), false>,
         {},
         {},
         {}});
    functions_["time.truncate"].push_back({"time.truncate",
                                           {BoundType::String(), BoundType::Any()},
                                           BoundType::Time(),
                                           false,
                                           false,
                                           scalar::temporalTruncateBatchFn<static_cast<uint8_t>(TimeKind::TIME), true>,
                                           {},
                                           {},
                                           {}});
    functions_["time.truncate"].push_back({"time.truncate",
                                           {BoundType::String(), BoundType::Any(), BoundType::Any()},
                                           BoundType::Time(),
                                           false,
                                           false,
                                           scalar::temporalTruncateBatchFn<static_cast<uint8_t>(TimeKind::TIME), true>,
                                           {},
                                           {},
                                           {}});
    functions_["localtime.truncate"].push_back(
        {"localtime.truncate",
         {BoundType::String(), BoundType::Any()},
         BoundType::Time(),
         false,
         false,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(TimeKind::LOCAL_TIME), true>,
         {},
         {},
         {}});
    functions_["localtime.truncate"].push_back(
        {"localtime.truncate",
         {BoundType::String(), BoundType::Any(), BoundType::Any()},
         BoundType::Time(),
         false,
         false,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(TimeKind::LOCAL_TIME), true>,
         {},
         {},
         {}});
    functions_["localdatetime.truncate"].push_back(
        {"localdatetime.truncate",
         {BoundType::String(), BoundType::DateTime()},
         BoundType::DateTime(),
         false,
         false,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(DateTimeKind::LOCAL_DATETIME), false>,
         {},
         {},
         {}});
    functions_["localdatetime.truncate"].push_back(
        {"localdatetime.truncate",
         {BoundType::String(), BoundType::DateTime(), BoundType::Any()},
         BoundType::DateTime(),
         false,
         false,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(DateTimeKind::LOCAL_DATETIME), false>,
         {},
         {},
         {}});

    // duration.between(DATETIME, DATETIME) -> Duration
    functions_["duration.between"].push_back({"duration.between",
                                              {BoundType::DateTime(), BoundType::DateTime()},
                                              BoundType::Duration(),
                                              false,
                                              false,
                                              scalar::durationBetweenBatchFn,
                                              {},
                                              {},
                                              {}});
    // duration.between(TIME, TIME) -> Duration
    functions_["duration.between"].push_back({"duration.between",
                                              {BoundType::Time(), BoundType::Time()},
                                              BoundType::Duration(),
                                              false,
                                              false,
                                              scalar::durationBetweenBatchFn,
                                              {},
                                              {},
                                              {}});
    // duration.between(DATETIME, TIME) -> Duration
    functions_["duration.between"].push_back({"duration.between",
                                              {BoundType::DateTime(), BoundType::Time()},
                                              BoundType::Duration(),
                                              false,
                                              false,
                                              scalar::durationBetweenBatchFn,
                                              {},
                                              {},
                                              {}});
    // duration.between(TIME, DATETIME) -> Duration
    functions_["duration.between"].push_back({"duration.between",
                                              {BoundType::Time(), BoundType::DateTime()},
                                              BoundType::Duration(),
                                              false,
                                              false,
                                              scalar::durationBetweenBatchFn,
                                              {},
                                              {},
                                              {}});

    // duration.inMonths(DATETIME, DATETIME) -> Duration
    functions_["duration.inMonths"].push_back({"duration.inMonths",
                                               {BoundType::DateTime(), BoundType::DateTime()},
                                               BoundType::Duration(),
                                               false,
                                               false,
                                               scalar::durationInMonthsBatchFn,
                                               {},
                                               {},
                                               {}});
    // duration.inMonths(TIME, TIME) -> Duration
    functions_["duration.inMonths"].push_back({"duration.inMonths",
                                               {BoundType::Time(), BoundType::Time()},
                                               BoundType::Duration(),
                                               false,
                                               false,
                                               scalar::durationInMonthsBatchFn,
                                               {},
                                               {},
                                               {}});
    // duration.inMonths(DATETIME, TIME) -> Duration
    functions_["duration.inMonths"].push_back({"duration.inMonths",
                                               {BoundType::DateTime(), BoundType::Time()},
                                               BoundType::Duration(),
                                               false,
                                               false,
                                               scalar::durationInMonthsBatchFn,
                                               {},
                                               {},
                                               {}});
    // duration.inMonths(TIME, DATETIME) -> Duration
    functions_["duration.inMonths"].push_back({"duration.inMonths",
                                               {BoundType::Time(), BoundType::DateTime()},
                                               BoundType::Duration(),
                                               false,
                                               false,
                                               scalar::durationInMonthsBatchFn,
                                               {},
                                               {},
                                               {}});

    // duration.inSeconds(DATETIME, DATETIME) -> Duration
    functions_["duration.inSeconds"].push_back({"duration.inSeconds",
                                                {BoundType::DateTime(), BoundType::DateTime()},
                                                BoundType::Duration(),
                                                false,
                                                false,
                                                scalar::durationInSecondsBatchFn,
                                                {},
                                                {},
                                                {}});
    // duration.inSeconds(TIME, TIME) -> Duration
    functions_["duration.inSeconds"].push_back({"duration.inSeconds",
                                                {BoundType::Time(), BoundType::Time()},
                                                BoundType::Duration(),
                                                false,
                                                false,
                                                scalar::durationInSecondsBatchFn,
                                                {},
                                                {},
                                                {}});
    // duration.inSeconds(DATETIME, TIME) -> Duration
    functions_["duration.inSeconds"].push_back({"duration.inSeconds",
                                                {BoundType::DateTime(), BoundType::Time()},
                                                BoundType::Duration(),
                                                false,
                                                false,
                                                scalar::durationInSecondsBatchFn,
                                                {},
                                                {},
                                                {}});
    // duration.inSeconds(TIME, DATETIME) -> Duration
    functions_["duration.inSeconds"].push_back({"duration.inSeconds",
                                                {BoundType::Time(), BoundType::DateTime()},
                                                BoundType::Duration(),
                                                false,
                                                false,
                                                scalar::durationInSecondsBatchFn,
                                                {},
                                                {},
                                                {}});

    // duration.inDays(DATETIME, DATETIME) -> Duration
    functions_["duration.inDays"].push_back({"duration.inDays",
                                             {BoundType::DateTime(), BoundType::DateTime()},
                                             BoundType::Duration(),
                                             false,
                                             false,
                                             scalar::durationInDaysBatchFn,
                                             {},
                                             {},
                                             {}});
    // duration.inDays(TIME, TIME) -> Duration
    functions_["duration.inDays"].push_back({"duration.inDays",
                                             {BoundType::Time(), BoundType::Time()},
                                             BoundType::Duration(),
                                             false,
                                             false,
                                             scalar::durationInDaysBatchFn,
                                             {},
                                             {},
                                             {}});
    // duration.inDays(DATETIME, TIME) -> Duration
    functions_["duration.inDays"].push_back({"duration.inDays",
                                             {BoundType::DateTime(), BoundType::Time()},
                                             BoundType::Duration(),
                                             false,
                                             false,
                                             scalar::durationInDaysBatchFn,
                                             {},
                                             {},
                                             {}});
    // duration.inDays(TIME, DATETIME) -> Duration
    functions_["duration.inDays"].push_back({"duration.inDays",
                                             {BoundType::Time(), BoundType::DateTime()},
                                             BoundType::Duration(),
                                             false,
                                             false,
                                             scalar::durationInDaysBatchFn,
                                             {},
                                             {},
                                             {}});

    // datetime.fromepoch(INT64) -> DateTime
    functions_["datetime.fromepoch"].push_back({"datetime.fromepoch",
                                                {BoundType::Int64()},
                                                BoundType::DateTime(),
                                                false,
                                                false,
                                                scalar::datetimeFromEpochBatchFn,
                                                {},
                                                {},
                                                {}});
    // datetime.fromepoch(INT64, INT64) -> DateTime
    functions_["datetime.fromepoch"].push_back({"datetime.fromepoch",
                                                {BoundType::Int64(), BoundType::Int64()},
                                                BoundType::DateTime(),
                                                false,
                                                false,
                                                scalar::datetimeFromEpochBatchFn,
                                                {},
                                                {},
                                                {}});

    // datetime.fromepochmillis(INT64) -> DateTime
    functions_["datetime.fromepochmillis"].push_back({"datetime.fromepochmillis",
                                                      {BoundType::Int64()},
                                                      BoundType::DateTime(),
                                                      false,
                                                      false,
                                                      scalar::datetimeFromEpochMillisBatchFn,
                                                      {},
                                                      {},
                                                      {}});

    // No-arg temporal accessor functions
    auto regNoArg = [this](const std::string& name, BoundType return_type, auto scalarFn, bool is_aggregate = false) {
        functions_[name].push_back(
            {name, {}, return_type, is_aggregate, false, scalarToBatch(function::ScalarFn(scalarFn)), {}, {}, {}, {}});
    };

    regNoArg("date.transaction", BoundType::DateTime(), scalar::dateTransactionScalarFn);
    regNoArg("date.statement", BoundType::DateTime(), scalar::dateStatementScalarFn);
    regNoArg("date.realtime", BoundType::DateTime(), scalar::dateRealtimeScalarFn);
    regNoArg("localtime.transaction", BoundType::Time(), scalar::localtimeTransactionScalarFn);
    regNoArg("localtime.statement", BoundType::Time(), scalar::localtimeStatementScalarFn);
    regNoArg("localtime.realtime", BoundType::Time(), scalar::localtimeRealtimeScalarFn);
    regNoArg("time.transaction", BoundType::Time(), scalar::timeTransactionScalarFn);
    regNoArg("time.statement", BoundType::Time(), scalar::timeStatementScalarFn);
    regNoArg("time.realtime", BoundType::Time(), scalar::timeRealtimeScalarFn);
    regNoArg("localdatetime.transaction", BoundType::DateTime(), scalar::localdatetimeTransactionScalarFn);
    regNoArg("localdatetime.statement", BoundType::DateTime(), scalar::localdatetimeStatementScalarFn);
    regNoArg("localdatetime.realtime", BoundType::DateTime(), scalar::localdatetimeRealtimeScalarFn);
    regNoArg("datetime.transaction", BoundType::DateTime(), scalar::datetimeTransactionScalarFn);
    regNoArg("datetime.statement", BoundType::DateTime(), scalar::datetimeStatementScalarFn);
    regNoArg("datetime.realtime", BoundType::DateTime(), scalar::datetimeRealtimeScalarFn);

    // Internal MATCH uniqueness predicate. Not user-visible; dbms.functions()
    // filters names starting with "__".
    FunctionDef edge_unique;
    edge_unique.name = "__edge_unique";
    edge_unique.return_type = BoundType::Bool();
    edge_unique.has_variadic_args = true;
    edge_unique.fn = edgeUniqueBatch;
    functions_["__edge_unique"].push_back(std::move(edge_unique));
}

namespace {

// Aggregate callbacks are captureless function pointers (see
// docs/query/engine/aggregate-typed-execution-design.md): no std::function
// allocation per group/row and no per-row vector<Value> argument container.
std::unique_ptr<AggStateBase> initCountState() {
    return std::make_unique<aggregate::CountState>();
}
void updateCountState(AggStateBase& s, const AggregateInput&) {
    static_cast<aggregate::CountState&>(s).add();
}
Value finalizeCountState(const AggStateBase& s) {
    return static_cast<const aggregate::CountState&>(s).finalize();
}

std::unique_ptr<AggStateBase> initInt64SumState() {
    return std::make_unique<aggregate::Int64SumState>();
}
void updateInt64SumState(AggStateBase& s, const AggregateInput& in) {
    static_cast<aggregate::Int64SumState&>(s).add(in.args[0]->getInt64(in.row));
}
Value finalizeInt64SumState(const AggStateBase& s) {
    return static_cast<const aggregate::Int64SumState&>(s).finalize();
}

std::unique_ptr<AggStateBase> initDoubleSumState() {
    return std::make_unique<aggregate::DoubleSumState>();
}
void updateDoubleSumState(AggStateBase& s, const AggregateInput& in) {
    static_cast<aggregate::DoubleSumState&>(s).add(in.args[0]->getDouble(in.row));
}
Value finalizeDoubleSumState(const AggStateBase& s) {
    return static_cast<const aggregate::DoubleSumState&>(s).finalize();
}

std::unique_ptr<AggStateBase> initAvgState() {
    return std::make_unique<aggregate::AvgState>();
}
void updateAvgState(AggStateBase& s, const AggregateInput& in) {
    static_cast<aggregate::AvgState&>(s).add(in.args[0]->getDouble(in.row));
}
Value finalizeAvgState(const AggStateBase& s) {
    return static_cast<const aggregate::AvgState&>(s).finalize();
}

std::unique_ptr<AggStateBase> initMinState() {
    return std::make_unique<aggregate::MinState>();
}
void updateMinState(AggStateBase& s, const AggregateInput& in) {
    static_cast<aggregate::MinState&>(s).add(in.args[0]->getValue(in.row));
}
Value finalizeMinState(const AggStateBase& s) {
    return static_cast<const aggregate::MinState&>(s).finalize();
}

std::unique_ptr<AggStateBase> initMaxState() {
    return std::make_unique<aggregate::MaxState>();
}
void updateMaxState(AggStateBase& s, const AggregateInput& in) {
    static_cast<aggregate::MaxState&>(s).add(in.args[0]->getValue(in.row));
}
Value finalizeMaxState(const AggStateBase& s) {
    return static_cast<const aggregate::MaxState&>(s).finalize();
}

std::unique_ptr<AggStateBase> initCollectState() {
    return std::make_unique<aggregate::CollectState>();
}
void updateCollectState(AggStateBase& s, const AggregateInput& in) {
    static_cast<aggregate::CollectState&>(s).add(in.args[0]->getValue(in.row));
}
Value finalizeCollectState(const AggStateBase& s) {
    return static_cast<const aggregate::CollectState&>(s).finalize();
}

std::unique_ptr<AggStateBase> initPercentileDiscState() {
    return std::make_unique<aggregate::PercentileDiscState>();
}
void updatePercentileDiscState(AggStateBase& s, const AggregateInput& in) {
    auto& st = static_cast<aggregate::PercentileDiscState&>(s);
    st.capturePercentile(in.args[1]->getValue(in.row));
    st.addValue(in.args[0]->getValue(in.row));
}
Value finalizePercentileDiscState(const AggStateBase& s) {
    return static_cast<const aggregate::PercentileDiscState&>(s).finalize();
}

std::unique_ptr<AggStateBase> initPercentileContState() {
    return std::make_unique<aggregate::PercentileContState>();
}
void updatePercentileContState(AggStateBase& s, const AggregateInput& in) {
    auto& st = static_cast<aggregate::PercentileContState&>(s);
    st.capturePercentile(in.args[1]->getValue(in.row));
    st.addValue(in.args[0]->getValue(in.row));
}
Value finalizePercentileContState(const AggStateBase& s) {
    return static_cast<const aggregate::PercentileContState&>(s).finalize();
}

} // namespace

void FunctionRegistry::registerAggregateBuiltins() {
    using binder::BoundType;

    auto add = [this](std::string name, std::vector<binder::BoundType> args, BoundType ret, bool variadic,
                      bool keeps_nulls, AggInitFn init, AggUpdateFn update, AggFinalizeFn finalize) {
        FunctionDef def;
        def.name = std::move(name);
        def.arg_types = std::move(args);
        def.return_type = std::move(ret);
        def.is_aggregate = true;
        def.has_variadic_args = variadic;
        def.keeps_nulls = keeps_nulls;
        def.agg_init = init;
        def.agg_update = update;
        def.agg_finalize = finalize;
        functions_[def.name].push_back(std::move(def));
    };

    // count(*) is represented as count with no typed args.
    add("count", {}, BoundType::Int64(), true, false, &initCountState, &updateCountState, &finalizeCountState);

    add("sum", {BoundType::Int64()}, BoundType::Int64(), false, false, &initInt64SumState, &updateInt64SumState,
        &finalizeInt64SumState);
    add("sum", {BoundType::Double()}, BoundType::Double(), false, false, &initDoubleSumState, &updateDoubleSumState,
        &finalizeDoubleSumState);

    add("avg", {BoundType::Int64()}, BoundType::Double(), false, false, &initAvgState, &updateAvgState,
        &finalizeAvgState);
    add("avg", {BoundType::Double()}, BoundType::Double(), false, false, &initAvgState, &updateAvgState,
        &finalizeAvgState);

    add("min", {}, BoundType::Any(), true, false, &initMinState, &updateMinState, &finalizeMinState);
    add("max", {}, BoundType::Any(), true, false, &initMaxState, &updateMaxState, &finalizeMaxState);
    add("collect", {}, BoundType::List(BoundType::Any()), true, false, &initCollectState, &updateCollectState,
        &finalizeCollectState);

    add("percentileDisc", {BoundType::Double(), BoundType::Double()}, BoundType::Double(), false, false,
        &initPercentileDiscState, &updatePercentileDiscState, &finalizePercentileDiscState);
    add("percentileDisc", {BoundType::Int64(), BoundType::Double()}, BoundType::Int64(), false, false,
        &initPercentileDiscState, &updatePercentileDiscState, &finalizePercentileDiscState);

    add("percentileCont", {BoundType::Double(), BoundType::Double()}, BoundType::Double(), false, false,
        &initPercentileContState, &updatePercentileContState, &finalizePercentileContState);
    add("percentileCont", {BoundType::Int64(), BoundType::Double()}, BoundType::Double(), false, false,
        &initPercentileContState, &updatePercentileContState, &finalizePercentileContState);
}

void FunctionRegistry::registerFunction(FunctionDef def) {
    functions_[def.name].push_back(std::move(def));
}

namespace {
std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void edgeUniqueBatch(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    result.reserve(count);
    // Reused across rows: the old scalar adapter allocated a fresh set for
    // every row; clearing a warmed set keeps the buckets instead.
    std::unordered_set<EdgeId> seen;
    seen.reserve(16);

    auto add_edge = [&](const Value& value) -> bool {
        if (isNull(value))
            return true;
        if (std::holds_alternative<EdgeValuePtr>(value)) {
            const auto& e = std::get<EdgeValuePtr>(value);
            if (!e || e->id == INVALID_EDGE_ID)
                return true;
            return seen.insert(e->id).second;
        }
        if (std::holds_alternative<EdgeKey>(value)) {
            const EdgeId id = std::get<EdgeKey>(value).id;
            if (id == INVALID_EDGE_ID)
                return true;
            return seen.insert(id).second;
        }
        return true;
    };

    auto check_value = [&](const Value& value) -> bool {
        if (std::holds_alternative<ListValuePtr>(value)) {
            const auto& list = std::get<ListValuePtr>(value);
            if (!list)
                return true;
            for (const auto& elem : list->elements) {
                if (!add_edge(elem.value))
                    return false;
            }
            return true;
        }
        return add_edge(value);
    };

    for (size_t row = 0; row < count; ++row) {
        seen.clear();
        bool unique = true;
        for (const auto* col : args) {
            if (!col)
                continue;
            // Columnar fast path for the common multi-edge MATCH columns.
            if (col->type == binder::BoundTypeKind::EDGE_KEY && col->form == VectorForm::FLAT && col->buffer &&
                row < col->buffer->edge_key_data.size()) {
                if (col->buffer->isNull(row))
                    continue;
                const EdgeId id = col->buffer->edge_key_data[row].id;
                if (id == INVALID_EDGE_ID)
                    continue;
                if (!seen.insert(id).second) {
                    unique = false;
                    break;
                }
                continue;
            }
            if (col->type == binder::BoundTypeKind::EDGE && col->form == VectorForm::FLAT && col->buffer &&
                row < col->buffer->edge_data.size()) {
                if (col->buffer->isNull(row))
                    continue;
                const auto& e = col->buffer->edge_data[row];
                if (!e || e->id == INVALID_EDGE_ID)
                    continue;
                if (!seen.insert(e->id).second) {
                    unique = false;
                    break;
                }
                continue;
            }
            if (!check_value(col->getValue(row))) {
                unique = false;
                break;
            }
        }
        result.setBool(row, unique);
    }
}
} // namespace

const FunctionDef* FunctionRegistry::lookup(const std::string& name,
                                            const std::vector<binder::BoundType>& arg_types) const {
    auto it = functions_.find(name);
    if (it == functions_.end()) {
        const std::string lower_name = lowerAscii(name);
        for (auto candidate = functions_.begin(); candidate != functions_.end(); ++candidate) {
            if (lowerAscii(candidate->first) == lower_name) {
                it = candidate;
                break;
            }
        }
    }
    if (it == functions_.end())
        return nullptr;

    const FunctionDef* best = nullptr;
    int best_cost = INT32_MAX;
    for (const auto& overload : it->second) {
        int cost = overload.matchCost(arg_types);
        if (cost >= 0 && cost < best_cost) {
            best_cost = cost;
            best = &overload;
        }
    }
    return best;
}

const std::vector<FunctionDef>* FunctionRegistry::lookupAll(const std::string& name) const {
    auto it = functions_.find(name);
    if (it == functions_.end())
        return nullptr;
    return &it->second;
}

bool FunctionRegistry::exists(const std::string& name) const {
    if (functions_.find(name) != functions_.end())
        return true;
    const std::string lower_name = lowerAscii(name);
    for (const auto& [candidate, overloads] : functions_) {
        (void)overloads;
        if (lowerAscii(candidate) == lower_name)
            return true;
    }
    return false;
}

std::vector<FunctionDef> FunctionRegistry::listFunctions() const {
    std::vector<FunctionDef> result;
    result.reserve(functions_.size());
    for (const auto& [name, overloads] : functions_) {
        (void)name;
        for (const auto& def : overloads)
            result.push_back(def);
    }
    return result;
}

} // namespace function
} // namespace eugraph
