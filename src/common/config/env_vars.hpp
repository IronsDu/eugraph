#pragma once

namespace eugraph::config {

/// Runtime environment variables read by the server/query engine.
///
/// Names live here so operational knobs are discoverable in one place; the
/// canonical user-facing reference is
/// docs/reference/environment-variables.md.
inline constexpr const char* kEnvNumaBind = "EUGRAPH_NUMA_BIND";
inline constexpr const char* kEnvOpTrace = "EUGRAPH_OP_TRACE";
inline constexpr const char* kEnvIoTrace = "EUGRAPH_IO_TRACE";
inline constexpr const char* kEnvPlanDebug = "EUGRAPH_PLAN_DEBUG";
inline constexpr const char* kEnvPredDebug = "EUGRAPH_PRED_DEBUG";
inline constexpr const char* kEnvDplDebug = "EUGRAPH_DPL_DEBUG";
inline constexpr const char* kEnvOptimizerForceRbo = "EUGRAPH_OPTIMIZER_FORCE_RBO";
inline constexpr const char* kEnvLeftJoinBatch = "EUGRAPH_LJ_BATCH";
inline constexpr const char* kEnvSemiJoinBatch = "EUGRAPH_SJ_BATCH";
inline constexpr const char* kEnvCacheMaxEdgesPerSource = "EUGRAPH_CACHE_MAX_EDGES_PER_SOURCE";
inline constexpr const char* kEnvProbeMaxEdgesPerSource = "EUGRAPH_PROBE_MAX_EDGES_PER_SOURCE";
inline constexpr const char* kEnvProbeMaxCachedEdges = "EUGRAPH_PROBE_MAX_CACHED_EDGES";
inline constexpr const char* kEnvExpandInputRows = "EUGRAPH_EXPAND_INPUT_ROWS";
inline constexpr const char* kEnvIndexBackfillBatch = "EUGRAPH_INDEX_BACKFILL_BATCH";
inline constexpr const char* kEnvPathCountCacheMaxEntries = "EUGRAPH_PATH_COUNT_CACHE_MAX_ENTRIES";

} // namespace eugraph::config
