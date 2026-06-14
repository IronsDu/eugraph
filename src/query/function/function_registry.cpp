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
#include "query/function/scalar/math_functions.hpp"
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

    // abs(Int64) -> Int64
    functions_["abs"].push_back({"abs",
                                 {BoundType::Int64()},
                                 BoundType::Int64(),
                                 false,
                                 false,
                                 scalar::absScalarFn,
                                 scalar::absBatchFn,
                                 {},
                                 {},
                                 {}});
    // abs(Double) -> Double
    functions_["abs"].push_back({"abs",
                                 {BoundType::Double()},
                                 BoundType::Double(),
                                 false,
                                 false,
                                 scalar::absScalarFn,
                                 scalar::absBatchFn,
                                 {},
                                 {},
                                 {}});

    // sign(Int64) -> Int64
    functions_["sign"].push_back({"sign",
                                  {BoundType::Int64()},
                                  BoundType::Int64(),
                                  false,
                                  false,
                                  scalar::signScalarFn,
                                  scalar::signBatchFn,
                                  {},
                                  {},
                                  {}});
    // sign(Double) -> Int64 (Cypher: returns -1/0/1 as integer)
    functions_["sign"].push_back({"sign",
                                  {BoundType::Double()},
                                  BoundType::Int64(),
                                  false,
                                  false,
                                  scalar::signScalarFn,
                                  scalar::signBatchFn,
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

    // reverse(String) -> String
    functions_["reverse"].push_back({"reverse",
                                     {BoundType::String()},
                                     BoundType::String(),
                                     false,
                                     false,
                                     scalar::reverseStringScalarFn,
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

    // sqrt(Double) -> Double
    functions_["sqrt"].push_back({"sqrt",
                                  {BoundType::Double()},
                                  BoundType::Double(),
                                  false,
                                  false,
                                  scalar::sqrtScalarFn,
                                  scalar::sqrtBatchFn,
                                  {},
                                  {},
                                  {}});

    // rand() -> Double
    functions_["rand"].push_back(
        {"rand", {}, BoundType::Double(), false, false, scalar::randScalarFn, scalar::randBatchFn, {}, {}, {}});

    // range(Any, Any) -> List<Int64>
    // Cypher defers argument type validation to runtime (ArgumentError /
    // InvalidArgumentType). Accept ANY at bind time; the scalar fn throws.
    functions_["range"].push_back({"range",
                                   {BoundType::Any(), BoundType::Any()},
                                   BoundType::List(BoundType::Int64()),
                                   false,
                                   false,
                                   scalar::rangeScalarFn,
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

    // toBoolean(Any) -> Boolean
    functions_["toBoolean"].push_back({"toBoolean",
                                       {},
                                       BoundType::Bool(),
                                       false,
                                       true,
                                       scalar::toBooleanScalarFn,
                                       scalar::toBooleanBatchFn,
                                       {},
                                       {},
                                       {}});

    // toString(Any) -> String
    functions_["toString"].push_back({"toString",
                                      {BoundType::Any()},
                                      BoundType::String(),
                                      false,
                                      false,
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

    // properties(Map<String, Any>) -> Map<String, Any> (identity)
    functions_["properties"].push_back({"properties",
                                        {BoundType::Map(BoundType::String(), BoundType::Any())},
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

    // toUpper(String) -> String
    functions_["toUpper"].push_back({"toUpper",
                                     {BoundType::String()},
                                     BoundType::String(),
                                     false,
                                     false,
                                     scalar::toUpperScalarFn,
                                     scalar::toUpperBatchFn,
                                     {},
                                     {},
                                     {}});

    // toLower(String) -> String
    functions_["toLower"].push_back({"toLower",
                                     {BoundType::String()},
                                     BoundType::String(),
                                     false,
                                     false,
                                     scalar::toLowerScalarFn,
                                     scalar::toLowerBatchFn,
                                     {},
                                     {},
                                     {}});

    // --- Coalesce ---

    // coalesce(Any...) -> Any
    functions_["coalesce"].push_back(
        {"coalesce", {}, BoundType::Any(), false, true, scalar::coalesceScalarFn, scalar::coalesceBatchFn, {}, {}, {}});

    // --- Temporal constructors ---

    // date() -> DateTime (0-arg: epoch 1970-01-01)
    functions_["date"].push_back(
        {"date", {}, BoundType::DateTime(), false, true, scalar::dateScalarFn, scalar::dateBatchFn, {}, {}, {}});
    // date(MAP<STRING, ANY>) -> DateTime
    functions_["date"].push_back({"date",
                                  {BoundType::Map(BoundType::String(), BoundType::Any())},
                                  BoundType::DateTime(),
                                  false,
                                  false,
                                  scalar::dateScalarFn,
                                  scalar::dateBatchFn,
                                  {},
                                  {},
                                  {}});
    // date(STRING) -> DateTime
    functions_["date"].push_back({"date",
                                  {BoundType::String()},
                                  BoundType::DateTime(),
                                  false,
                                  false,
                                  scalar::dateScalarFn,
                                  scalar::dateBatchFn,
                                  {},
                                  {},
                                  {}});

    // localtime() -> Time (0-arg: epoch 00:00:00)
    functions_["localtime"].push_back({"localtime",
                                       {},
                                       BoundType::Time(),
                                       false,
                                       true,
                                       scalar::localtimeScalarFn,
                                       scalar::localtimeBatchFn,
                                       {},
                                       {},
                                       {}});
    // localtime(MAP<STRING, ANY>) -> Time
    functions_["localtime"].push_back({"localtime",
                                       {BoundType::Map(BoundType::String(), BoundType::Any())},
                                       BoundType::Time(),
                                       false,
                                       false,
                                       scalar::localtimeScalarFn,
                                       scalar::localtimeBatchFn,
                                       {},
                                       {},
                                       {}});
    // localtime(STRING) -> Time
    functions_["localtime"].push_back({"localtime",
                                       {BoundType::String()},
                                       BoundType::Time(),
                                       false,
                                       false,
                                       scalar::localtimeScalarFn,
                                       scalar::localtimeBatchFn,
                                       {},
                                       {},
                                       {}});

    // time() -> Time (0-arg: epoch 00:00:00Z)
    functions_["time"].push_back(
        {"time", {}, BoundType::Time(), false, true, scalar::timeScalarFn, scalar::timeBatchFn, {}, {}, {}});
    // time(MAP<STRING, ANY>) -> Time
    functions_["time"].push_back({"time",
                                  {BoundType::Map(BoundType::String(), BoundType::Any())},
                                  BoundType::Time(),
                                  false,
                                  false,
                                  scalar::timeScalarFn,
                                  scalar::timeBatchFn,
                                  {},
                                  {},
                                  {}});
    // time(STRING) -> Time
    functions_["time"].push_back({"time",
                                  {BoundType::String()},
                                  BoundType::Time(),
                                  false,
                                  false,
                                  scalar::timeScalarFn,
                                  scalar::timeBatchFn,
                                  {},
                                  {},
                                  {}});

    // localdatetime() -> DateTime (0-arg: epoch 1970-01-01T00:00:00)
    functions_["localdatetime"].push_back({"localdatetime",
                                           {},
                                           BoundType::DateTime(),
                                           false,
                                           true,
                                           scalar::localdatetimeScalarFn,
                                           scalar::localdatetimeBatchFn,
                                           {},
                                           {},
                                           {}});
    // localdatetime(MAP<STRING, ANY>) -> DateTime
    functions_["localdatetime"].push_back({"localdatetime",
                                           {BoundType::Map(BoundType::String(), BoundType::Any())},
                                           BoundType::DateTime(),
                                           false,
                                           false,
                                           scalar::localdatetimeScalarFn,
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
                                           scalar::localdatetimeScalarFn,
                                           scalar::localdatetimeBatchFn,
                                           {},
                                           {},
                                           {}});

    // datetime() -> DateTime (0-arg: epoch 1970-01-01T00:00:00Z)
    functions_["datetime"].push_back({"datetime",
                                      {},
                                      BoundType::DateTime(),
                                      false,
                                      true,
                                      scalar::datetimeScalarFn,
                                      scalar::datetimeBatchFn,
                                      {},
                                      {},
                                      {}});
    // datetime(MAP<STRING, ANY>) -> DateTime
    functions_["datetime"].push_back({"datetime",
                                      {BoundType::Map(BoundType::String(), BoundType::Any())},
                                      BoundType::DateTime(),
                                      false,
                                      false,
                                      scalar::datetimeScalarFn,
                                      scalar::datetimeBatchFn,
                                      {},
                                      {},
                                      {}});
    // datetime(STRING) -> DateTime
    functions_["datetime"].push_back({"datetime",
                                      {BoundType::String()},
                                      BoundType::DateTime(),
                                      false,
                                      false,
                                      scalar::datetimeScalarFn,
                                      scalar::datetimeBatchFn,
                                      {},
                                      {},
                                      {}});

    // duration(MAP<STRING, ANY>) -> Duration
    functions_["duration"].push_back({"duration",
                                      {BoundType::Map(BoundType::String(), BoundType::Any())},
                                      BoundType::Duration(),
                                      false,
                                      false,
                                      scalar::durationScalarFn,
                                      scalar::durationBatchFn,
                                      {},
                                      {},
                                      {}});
    // duration(STRING) -> Duration
    functions_["duration"].push_back({"duration",
                                      {BoundType::String()},
                                      BoundType::Duration(),
                                      false,
                                      false,
                                      scalar::durationScalarFn,
                                      scalar::durationBatchFn,
                                      {},
                                      {},
                                      {}});

    // __temporal_field__(ANY, INT64) -> Any
    functions_["__temporal_field__"].push_back({"__temporal_field__",
                                                {BoundType::Any(), BoundType::Int64()},
                                                BoundType::Any(),
                                                false,
                                                false,
                                                scalar::temporalAccessorScalarFn,
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
                                                scalar::temporalAccessorScalarFn,
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
                                                scalar::temporalAccessorScalarFn,
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
         scalar::temporalTruncateScalarFn<static_cast<uint8_t>(DateTimeKind::DATE), false>,
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
         scalar::temporalTruncateScalarFn<static_cast<uint8_t>(DateTimeKind::DATE), false>,
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
         scalar::temporalTruncateScalarFn<static_cast<uint8_t>(DateTimeKind::DATETIME), false>,
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
         scalar::temporalTruncateScalarFn<static_cast<uint8_t>(DateTimeKind::DATETIME), false>,
         scalar::temporalTruncateBatchFn<static_cast<uint8_t>(DateTimeKind::DATETIME), false>,
         {},
         {},
         {}});
    functions_["time.truncate"].push_back({"time.truncate",
                                           {BoundType::String(), BoundType::Any()},
                                           BoundType::Time(),
                                           false,
                                           false,
                                           scalar::temporalTruncateScalarFn<static_cast<uint8_t>(TimeKind::TIME), true>,
                                           scalar::temporalTruncateBatchFn<static_cast<uint8_t>(TimeKind::TIME), true>,
                                           {},
                                           {},
                                           {}});
    functions_["time.truncate"].push_back({"time.truncate",
                                           {BoundType::String(), BoundType::Any(), BoundType::Any()},
                                           BoundType::Time(),
                                           false,
                                           false,
                                           scalar::temporalTruncateScalarFn<static_cast<uint8_t>(TimeKind::TIME), true>,
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
         scalar::temporalTruncateScalarFn<static_cast<uint8_t>(TimeKind::LOCAL_TIME), true>,
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
         scalar::temporalTruncateScalarFn<static_cast<uint8_t>(TimeKind::LOCAL_TIME), true>,
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
         scalar::temporalTruncateScalarFn<static_cast<uint8_t>(DateTimeKind::LOCAL_DATETIME), false>,
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
         scalar::temporalTruncateScalarFn<static_cast<uint8_t>(DateTimeKind::LOCAL_DATETIME), false>,
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
                                              scalar::durationBetweenScalarFn,
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
                                              scalar::durationBetweenScalarFn,
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
                                              scalar::durationBetweenScalarFn,
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
                                              scalar::durationBetweenScalarFn,
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
                                               scalar::durationInMonthsScalarFn,
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
                                               scalar::durationInMonthsScalarFn,
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
                                               scalar::durationInMonthsScalarFn,
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
                                               scalar::durationInMonthsScalarFn,
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
                                                scalar::durationInSecondsScalarFn,
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
                                                scalar::durationInSecondsScalarFn,
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
                                                scalar::durationInSecondsScalarFn,
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
                                                scalar::durationInSecondsScalarFn,
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
                                             scalar::durationInDaysScalarFn,
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
                                             scalar::durationInDaysScalarFn,
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
                                             scalar::durationInDaysScalarFn,
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
                                             scalar::durationInDaysScalarFn,
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
                                                scalar::datetimeFromEpochScalarFn,
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
                                                scalar::datetimeFromEpochScalarFn,
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
                                                      scalar::datetimeFromEpochMillisScalarFn,
                                                      scalar::datetimeFromEpochMillisBatchFn,
                                                      {},
                                                      {},
                                                      {}});

    // No-arg temporal accessor functions
    auto regNoArg = [this](const std::string& name, BoundType return_type, auto scalarFn, bool is_aggregate = false) {
        functions_[name].push_back({name, {}, return_type, is_aggregate, false, scalarFn, {}, {}, {}, {}});
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
