#pragma once
/// 事务 schema pin 的唯一绑定入口。
///
/// 目前 QueryExecutor 的语句路径和批量装载都需要：
///   * 在事务开始时固定一个不可变 GraphSchema 版本；
///   * 在 schema_generation 上计一次 pin，事务结束自动 unpin；
///   * 保证 pin 早于索引构建开始的事务会被 drain。
///
/// 这里不持有 GraphSession/GraphSchema 所有权，只做"绑定/计数/回调"，
/// 便于单测和以后接事务级 schema overlay。

#include "common/schema/schema_version.hpp"
#include "storage/session/graph_session.hpp"

#include <cstdint>
#include <memory>

namespace eugraph {

class SchemaGenerationTracker;

/// 为当前事务绑定 schema 版本；已有有效 pin 时只补 tracker 计数。
/// 返回绑定后的 pin（引用随 GraphSession 生命周期有效）。
inline const GraphSession::SchemaPin& ensureSchemaPin(GraphSession& session, const SchemaVersion& version,
                                                      SchemaGenerationTracker* tracker) {
    auto pin = session.schemaPin();
    if (!pin.valid || !pin.schema) {
        pin = GraphSession::SchemaPin{version.generation, version.compat_version, true, false, version.schema};
        session.setSchemaPin(std::move(pin));
    } else if (!pin.schema && version.schema) {
        pin.schema = version.schema;
        session.setSchemaPin(std::move(pin));
    }

    if (!session.schemaPin().tracked && tracker != nullptr) {
        const uint64_t generation = session.schemaPin().generation;
        tracker->pin(generation);
        session.markSchemaPinTracked();
        session.onTransactionEnd([tracker, generation] { tracker->unpin(generation); });
    }
    return session.schemaPin();
}

} // namespace eugraph
