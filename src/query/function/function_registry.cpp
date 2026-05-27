#include "query/function/function_registry.hpp"

#include <climits>

#include "query/function/aggregate/avg_function.hpp"
#include "query/function/aggregate/collect_function.hpp"
#include "query/function/aggregate/count_function.hpp"
#include "query/function/aggregate/min_function.hpp"
#include "query/function/aggregate/min_function.hpp" // also provides MaxState
#include "query/function/aggregate/sum_function.hpp"
#include "query/function/scalar/coalesce_function.hpp"
#include "query/function/scalar/conversion_functions.hpp"
#include "query/function/scalar/graph_functions.hpp"
#include "query/function/scalar/id_function.hpp"
#include "query/function/scalar/list_functions.hpp"
#include "query/function/scalar/path_functions.hpp"
#include "query/function/scalar/string_functions.hpp"
#include "query/function/scalar/temporal_functions.hpp"
#include "query/function/scalar/type_function.hpp"

namespace eugraph {
namespace function {

FunctionRegistry::FunctionRegistry() {
    registerBuiltins();
}

void FunctionRegistry::registerBuiltins() {
    registerScalarBuiltins();
    registerAggregateBuiltins();
}

void FunctionRegistry::registerScalarBuiltins() {
    using binder::BoundType;

    // id(Vertex) -> Int64
    functions_["id"].push_back({"id",
                                {BoundType::Vertex()},
                                BoundType::Int64(),
                                false,
                                false,
                                scalar::idScalarFn,
                                scalar::idBatchFn,
                                {},
                                {},
                                {}});
    // id(Edge) -> Int64
    functions_["id"].push_back({"id",
                                {BoundType::Edge()},
                                BoundType::Int64(),
                                false,
                                false,
                                scalar::idScalarFn,
                                scalar::idBatchFn,
                                {},
                                {},
                                {}});

    // nodes(Path) -> List<Vertex>
    functions_["nodes"].push_back({"nodes",
                                   {BoundType::Path()},
                                   BoundType::List(BoundType::Vertex()),
                                   false,
                                   false,
                                   scalar::nodesScalarFn,
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
                                           scalar::relationshipsScalarFn,
                                           scalar::relationshipsBatchFn,
                                           {},
                                           {},
                                           {}});

    // length(Path) -> Int64
    functions_["length"].push_back({"length",
                                    {BoundType::Path()},
                                    BoundType::Int64(),
                                    false,
                                    false,
                                    scalar::lengthScalarFn,
                                    scalar::lengthBatchFn,
                                    {},
                                    {},
                                    {}});

    // type(Edge) -> String
    functions_["type"].push_back({"type",
                                  {BoundType::Edge()},
                                  BoundType::String(),
                                  false,
                                  false,
                                  scalar::typeScalarFn,
                                  scalar::typeBatchFn,
                                  {},
                                  {},
                                  {}});

    // last(List<Any>) -> Any
    functions_["last"].push_back({"last",
                                  {BoundType::List(BoundType::Any())},
                                  BoundType::Any(),
                                  false,
                                  false,
                                  scalar::lastScalarFn,
                                  scalar::lastBatchFn,
                                  {},
                                  {},
                                  {}});

    // head(List<Any>) -> Any
    functions_["head"].push_back({"head",
                                  {BoundType::List(BoundType::Any())},
                                  BoundType::Any(),
                                  false,
                                  false,
                                  scalar::headScalarFn,
                                  scalar::headBatchFn,
                                  {},
                                  {},
                                  {}});

    // tail(List<Any>) -> List<Any>
    functions_["tail"].push_back({"tail",
                                  {BoundType::List(BoundType::Any())},
                                  BoundType::List(BoundType::Any()),
                                  false,
                                  false,
                                  scalar::tailScalarFn,
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
                                     scalar::reverseScalarFn,
                                     scalar::reverseBatchFn,
                                     {},
                                     {},
                                     {}});

    // size(List<Any>) -> Int64
    functions_["size"].push_back({"size",
                                  {BoundType::List(BoundType::Any())},
                                  BoundType::Int64(),
                                  false,
                                  false,
                                  scalar::sizeScalarFn,
                                  scalar::sizeListBatchFn,
                                  {},
                                  {},
                                  {}});

    // size(String) -> Int64
    functions_["size"].push_back({"size",
                                  {BoundType::String()},
                                  BoundType::Int64(),
                                  false,
                                  false,
                                  scalar::sizeScalarFn,
                                  scalar::sizeStringBatchFn,
                                  {},
                                  {},
                                  {}});

    // range(Int64, Int64) -> List<Int64>
    functions_["range"].push_back({"range",
                                   {BoundType::Int64(), BoundType::Int64()},
                                   BoundType::List(BoundType::Int64()),
                                   false,
                                   false,
                                   scalar::rangeScalarFn,
                                   scalar::rangeBatchFn,
                                   {},
                                   {},
                                   {}});

    // range(Int64, Int64, Int64) -> List<Int64>
    functions_["range"].push_back({"range",
                                   {BoundType::Int64(), BoundType::Int64(), BoundType::Int64()},
                                   BoundType::List(BoundType::Int64()),
                                   false,
                                   false,
                                   scalar::rangeScalarFn,
                                   scalar::rangeBatchFn,
                                   {},
                                   {},
                                   {}});

    // toInteger(Any) -> Int64
    functions_["toInteger"].push_back({"toInteger",
                                       {},
                                       BoundType::Int64(),
                                       false,
                                       true,
                                       scalar::toIntegerScalarFn,
                                       scalar::toIntegerBatchFn,
                                       {},
                                       {},
                                       {}});

    // toFloat(Any) -> Double
    functions_["toFloat"].push_back(
        {"toFloat", {}, BoundType::Double(), false, true, scalar::toFloatScalarFn, scalar::toFloatBatchFn, {}, {}, {}});

    // toString(Any) -> String
    functions_["toString"].push_back({"toString",
                                      {},
                                      BoundType::String(),
                                      false,
                                      true,
                                      scalar::toStringScalarFn,
                                      scalar::toStringBatchFn,
                                      {},
                                      {},
                                      {}});

    // labels(Vertex) -> List<String>
    functions_["labels"].push_back({"labels",
                                    {BoundType::Vertex()},
                                    BoundType::List(BoundType::String()),
                                    false,
                                    false,
                                    scalar::labelsScalarFn,
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
                                  scalar::keysScalarFn,
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
                                  scalar::keysScalarFn,
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
                                  scalar::keysScalarFn,
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
                                        scalar::propertiesScalarFn,
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
                                        scalar::propertiesScalarFn,
                                        scalar::propertiesBatchFn,
                                        {},
                                        {},
                                        {}});

    // --- String functions ---

    // trim(String) -> String
    functions_["trim"].push_back({"trim",
                                  {BoundType::String()},
                                  BoundType::String(),
                                  false,
                                  false,
                                  scalar::trimScalarFn,
                                  scalar::trimBatchFn,
                                  {},
                                  {},
                                  {}});

    // ltrim(String) -> String
    functions_["ltrim"].push_back({"ltrim",
                                   {BoundType::String()},
                                   BoundType::String(),
                                   false,
                                   false,
                                   scalar::ltrimScalarFn,
                                   scalar::ltrimBatchFn,
                                   {},
                                   {},
                                   {}});

    // rtrim(String) -> String
    functions_["rtrim"].push_back({"rtrim",
                                   {BoundType::String()},
                                   BoundType::String(),
                                   false,
                                   false,
                                   scalar::rtrimScalarFn,
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
                                   scalar::splitScalarFn,
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
                                     scalar::replaceScalarFn,
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
                                       scalar::substringScalarFn,
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
                                       scalar::substringScalarFn,
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
                                  scalar::leftScalarFn,
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
                                   scalar::rightScalarFn,
                                   scalar::rightBatchFn,
                                   {},
                                   {},
                                   {}});

    // --- Coalesce ---

    // coalesce(Any...) -> Any
    functions_["coalesce"].push_back(
        {"coalesce", {}, BoundType::Any(), false, true, scalar::coalesceScalarFn, scalar::coalesceBatchFn, {}, {}, {}});

    // --- Temporal constructors ---

    // date() -> Temporal (0-arg: epoch 1970-01-01)
    functions_["date"].push_back(
        {"date", {}, BoundType::Temporal(), false, true, scalar::dateScalarFn, scalar::dateBatchFn, {}, {}, {}});
    // date(MAP<STRING, ANY>) -> Temporal
    functions_["date"].push_back({"date",
                                  {BoundType::Map(BoundType::String(), BoundType::Any())},
                                  BoundType::Temporal(),
                                  false,
                                  false,
                                  scalar::dateScalarFn,
                                  scalar::dateBatchFn,
                                  {},
                                  {},
                                  {}});
    // date(STRING) -> Temporal
    functions_["date"].push_back({"date",
                                  {BoundType::String()},
                                  BoundType::Temporal(),
                                  false,
                                  false,
                                  scalar::dateScalarFn,
                                  scalar::dateBatchFn,
                                  {},
                                  {},
                                  {}});

    // localtime() -> Temporal (0-arg: epoch 00:00:00)
    functions_["localtime"].push_back({"localtime",
                                       {},
                                       BoundType::Temporal(),
                                       false,
                                       true,
                                       scalar::localtimeScalarFn,
                                       scalar::localtimeBatchFn,
                                       {},
                                       {},
                                       {}});
    // localtime(MAP<STRING, ANY>) -> Temporal
    functions_["localtime"].push_back({"localtime",
                                       {BoundType::Map(BoundType::String(), BoundType::Any())},
                                       BoundType::Temporal(),
                                       false,
                                       false,
                                       scalar::localtimeScalarFn,
                                       scalar::localtimeBatchFn,
                                       {},
                                       {},
                                       {}});
    // localtime(STRING) -> Temporal
    functions_["localtime"].push_back({"localtime",
                                       {BoundType::String()},
                                       BoundType::Temporal(),
                                       false,
                                       false,
                                       scalar::localtimeScalarFn,
                                       scalar::localtimeBatchFn,
                                       {},
                                       {},
                                       {}});

    // time() -> Temporal (0-arg: epoch 00:00:00Z)
    functions_["time"].push_back(
        {"time", {}, BoundType::Temporal(), false, true, scalar::timeScalarFn, scalar::timeBatchFn, {}, {}, {}});
    // time(MAP<STRING, ANY>) -> Temporal
    functions_["time"].push_back({"time",
                                  {BoundType::Map(BoundType::String(), BoundType::Any())},
                                  BoundType::Temporal(),
                                  false,
                                  false,
                                  scalar::timeScalarFn,
                                  scalar::timeBatchFn,
                                  {},
                                  {},
                                  {}});
    // time(STRING) -> Temporal
    functions_["time"].push_back({"time",
                                  {BoundType::String()},
                                  BoundType::Temporal(),
                                  false,
                                  false,
                                  scalar::timeScalarFn,
                                  scalar::timeBatchFn,
                                  {},
                                  {},
                                  {}});

    // localdatetime() -> Temporal (0-arg: epoch 1970-01-01T00:00:00)
    functions_["localdatetime"].push_back({"localdatetime",
                                           {},
                                           BoundType::Temporal(),
                                           false,
                                           true,
                                           scalar::localdatetimeScalarFn,
                                           scalar::localdatetimeBatchFn,
                                           {},
                                           {},
                                           {}});
    // localdatetime(MAP<STRING, ANY>) -> Temporal
    functions_["localdatetime"].push_back({"localdatetime",
                                           {BoundType::Map(BoundType::String(), BoundType::Any())},
                                           BoundType::Temporal(),
                                           false,
                                           false,
                                           scalar::localdatetimeScalarFn,
                                           scalar::localdatetimeBatchFn,
                                           {},
                                           {},
                                           {}});
    // localdatetime(STRING) -> Temporal
    functions_["localdatetime"].push_back({"localdatetime",
                                           {BoundType::String()},
                                           BoundType::Temporal(),
                                           false,
                                           false,
                                           scalar::localdatetimeScalarFn,
                                           scalar::localdatetimeBatchFn,
                                           {},
                                           {},
                                           {}});

    // datetime() -> Temporal (0-arg: epoch 1970-01-01T00:00:00Z)
    functions_["datetime"].push_back({"datetime",
                                      {},
                                      BoundType::Temporal(),
                                      false,
                                      true,
                                      scalar::datetimeScalarFn,
                                      scalar::datetimeBatchFn,
                                      {},
                                      {},
                                      {}});
    // datetime(MAP<STRING, ANY>) -> Temporal
    functions_["datetime"].push_back({"datetime",
                                      {BoundType::Map(BoundType::String(), BoundType::Any())},
                                      BoundType::Temporal(),
                                      false,
                                      false,
                                      scalar::datetimeScalarFn,
                                      scalar::datetimeBatchFn,
                                      {},
                                      {},
                                      {}});
    // datetime(STRING) -> Temporal
    functions_["datetime"].push_back({"datetime",
                                      {BoundType::String()},
                                      BoundType::Temporal(),
                                      false,
                                      false,
                                      scalar::datetimeScalarFn,
                                      scalar::datetimeBatchFn,
                                      {},
                                      {},
                                      {}});

    // duration(MAP<STRING, ANY>) -> Temporal
    functions_["duration"].push_back({"duration",
                                      {BoundType::Map(BoundType::String(), BoundType::Any())},
                                      BoundType::Temporal(),
                                      false,
                                      false,
                                      scalar::durationScalarFn,
                                      scalar::durationBatchFn,
                                      {},
                                      {},
                                      {}});
    // duration(STRING) -> Temporal
    functions_["duration"].push_back({"duration",
                                      {BoundType::String()},
                                      BoundType::Temporal(),
                                      false,
                                      false,
                                      scalar::durationScalarFn,
                                      scalar::durationBatchFn,
                                      {},
                                      {},
                                      {}});

    // __temporal_field__(TEMPORAL, INT64) -> Any
    functions_["__temporal_field__"].push_back({"__temporal_field__",
                                                {BoundType::Temporal(), BoundType::Int64()},
                                                BoundType::Any(),
                                                false,
                                                false,
                                                scalar::temporalAccessorScalarFn,
                                                scalar::temporalAccessorBatchFn,
                                                {},
                                                {},
                                                {}});

    // truncate(STRING, TEMPORAL) -> TEMPORAL  (2-arg)
    // truncate(STRING, TEMPORAL, ANY) -> TEMPORAL  (3-arg)
    functions_["date.truncate"].push_back({"date.truncate",
                                           {BoundType::String(), BoundType::Temporal()},
                                           BoundType::Temporal(),
                                           false,
                                           false,
                                           scalar::temporalTruncateScalarFn<TemporalKind::DATE>,
                                           scalar::temporalTruncateBatchFn<TemporalKind::DATE>,
                                           {},
                                           {},
                                           {}});
    functions_["date.truncate"].push_back({"date.truncate",
                                           {BoundType::String(), BoundType::Temporal(), BoundType::Any()},
                                           BoundType::Temporal(),
                                           false,
                                           false,
                                           scalar::temporalTruncateScalarFn<TemporalKind::DATE>,
                                           scalar::temporalTruncateBatchFn<TemporalKind::DATE>,
                                           {},
                                           {},
                                           {}});
    functions_["datetime.truncate"].push_back({"datetime.truncate",
                                               {BoundType::String(), BoundType::Temporal()},
                                               BoundType::Temporal(),
                                               false,
                                               false,
                                               scalar::temporalTruncateScalarFn<TemporalKind::DATETIME>,
                                               scalar::temporalTruncateBatchFn<TemporalKind::DATETIME>,
                                               {},
                                               {},
                                               {}});
    functions_["datetime.truncate"].push_back({"datetime.truncate",
                                               {BoundType::String(), BoundType::Temporal(), BoundType::Any()},
                                               BoundType::Temporal(),
                                               false,
                                               false,
                                               scalar::temporalTruncateScalarFn<TemporalKind::DATETIME>,
                                               scalar::temporalTruncateBatchFn<TemporalKind::DATETIME>,
                                               {},
                                               {},
                                               {}});
    functions_["time.truncate"].push_back({"time.truncate",
                                           {BoundType::String(), BoundType::Temporal()},
                                           BoundType::Temporal(),
                                           false,
                                           false,
                                           scalar::temporalTruncateScalarFn<TemporalKind::TIME>,
                                           scalar::temporalTruncateBatchFn<TemporalKind::TIME>,
                                           {},
                                           {},
                                           {}});
    functions_["time.truncate"].push_back({"time.truncate",
                                           {BoundType::String(), BoundType::Temporal(), BoundType::Any()},
                                           BoundType::Temporal(),
                                           false,
                                           false,
                                           scalar::temporalTruncateScalarFn<TemporalKind::TIME>,
                                           scalar::temporalTruncateBatchFn<TemporalKind::TIME>,
                                           {},
                                           {},
                                           {}});
    functions_["localtime.truncate"].push_back({"localtime.truncate",
                                                {BoundType::String(), BoundType::Temporal()},
                                                BoundType::Temporal(),
                                                false,
                                                false,
                                                scalar::temporalTruncateScalarFn<TemporalKind::LOCAL_TIME>,
                                                scalar::temporalTruncateBatchFn<TemporalKind::LOCAL_TIME>,
                                                {},
                                                {},
                                                {}});
    functions_["localtime.truncate"].push_back({"localtime.truncate",
                                                {BoundType::String(), BoundType::Temporal(), BoundType::Any()},
                                                BoundType::Temporal(),
                                                false,
                                                false,
                                                scalar::temporalTruncateScalarFn<TemporalKind::LOCAL_TIME>,
                                                scalar::temporalTruncateBatchFn<TemporalKind::LOCAL_TIME>,
                                                {},
                                                {},
                                                {}});
    functions_["localdatetime.truncate"].push_back({"localdatetime.truncate",
                                                    {BoundType::String(), BoundType::Temporal()},
                                                    BoundType::Temporal(),
                                                    false,
                                                    false,
                                                    scalar::temporalTruncateScalarFn<TemporalKind::LOCAL_DATETIME>,
                                                    scalar::temporalTruncateBatchFn<TemporalKind::LOCAL_DATETIME>,
                                                    {},
                                                    {},
                                                    {}});
    functions_["localdatetime.truncate"].push_back({"localdatetime.truncate",
                                                    {BoundType::String(), BoundType::Temporal(), BoundType::Any()},
                                                    BoundType::Temporal(),
                                                    false,
                                                    false,
                                                    scalar::temporalTruncateScalarFn<TemporalKind::LOCAL_DATETIME>,
                                                    scalar::temporalTruncateBatchFn<TemporalKind::LOCAL_DATETIME>,
                                                    {},
                                                    {},
                                                    {}});

    // duration.between(TEMPORAL, TEMPORAL) -> TEMPORAL
    functions_["duration.between"].push_back({"duration.between",
                                              {BoundType::Temporal(), BoundType::Temporal()},
                                              BoundType::Temporal(),
                                              false,
                                              false,
                                              scalar::durationBetweenScalarFn,
                                              scalar::durationBetweenBatchFn,
                                              {},
                                              {},
                                              {}});

    // duration.inMonths(TEMPORAL, TEMPORAL) -> TEMPORAL
    functions_["duration.inMonths"].push_back({"duration.inMonths",
                                               {BoundType::Temporal(), BoundType::Temporal()},
                                               BoundType::Temporal(),
                                               false,
                                               false,
                                               scalar::durationInMonthsScalarFn,
                                               scalar::durationInMonthsBatchFn,
                                               {},
                                               {},
                                               {}});
}

void FunctionRegistry::registerAggregateBuiltins() {
    using binder::BoundType;

    // count(*) -> Int64  (represented as count with no typed args)
    functions_["count"].push_back(
        {"count",
         {},
         BoundType::Int64(),
         true,
         true,
         {},
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::CountState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::CountState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::CountState&>(s).finalize(); }});

    // sum(Int64) -> Int64
    functions_["sum"].push_back(
        {"sum",
         {BoundType::Int64()},
         BoundType::Int64(),
         true,
         false,
         {},
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::Int64SumState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::Int64SumState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::Int64SumState&>(s).finalize(); }});
    // sum(Double) -> Double
    functions_["sum"].push_back(
        {"sum",
         {BoundType::Double()},
         BoundType::Double(),
         true,
         false,
         {},
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::DoubleSumState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::DoubleSumState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::DoubleSumState&>(s).finalize(); }});

    // avg(Int64) -> Double (avg always returns double)
    functions_["avg"].push_back(
        {"avg",
         {BoundType::Int64()},
         BoundType::Double(),
         true,
         false,
         {},
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::AvgState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::AvgState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::AvgState&>(s).finalize(); }});
    // avg(Double) -> Double
    functions_["avg"].push_back(
        {"avg",
         {BoundType::Double()},
         BoundType::Double(),
         true,
         false,
         {},
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::AvgState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::AvgState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::AvgState&>(s).finalize(); }});

    // min(Any) -> Any
    functions_["min"].push_back(
        {"min",
         {},
         BoundType::Any(),
         true,
         true,
         {},
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::MinState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::MinState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::MinState&>(s).finalize(); }});

    // max(Any) -> Any
    functions_["max"].push_back(
        {"max",
         {},
         BoundType::Any(),
         true,
         true,
         {},
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::MaxState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::MaxState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::MaxState&>(s).finalize(); }});

    // collect(Any) -> List<Any>
    functions_["collect"].push_back(
        {"collect",
         {},
         BoundType::List(BoundType::Any()),
         true,
         true,
         {},
         {},
         []() -> std::unique_ptr<AggStateBase> { return std::make_unique<aggregate::CollectState>(); },
         [](AggStateBase& s, const Value& v) { static_cast<aggregate::CollectState&>(s).add(v); },
         [](const AggStateBase& s) -> Value { return static_cast<const aggregate::CollectState&>(s).finalize(); }});
}

void FunctionRegistry::registerFunction(FunctionDef def) {
    functions_[def.name].push_back(std::move(def));
}

const FunctionDef* FunctionRegistry::lookup(const std::string& name,
                                            const std::vector<binder::BoundType>& arg_types) const {
    auto it = functions_.find(name);
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
    return functions_.find(name) != functions_.end();
}

} // namespace function
} // namespace eugraph
