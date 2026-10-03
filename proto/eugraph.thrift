namespace cpp2 eugraph.thrift_service

// ==================== Common Types ====================

enum PropertyType {
  BOOL = 1,
  INT64 = 2,
  DOUBLE = 3,
  STRING = 4,
  INT64_ARRAY = 5,
  DOUBLE_ARRAY = 6,
  STRING_ARRAY = 7,
  DATETIME = 8,
  TIME = 9,
  DURATION = 10,
  DATETIME_ARRAY = 11,
  TIME_ARRAY = 12,
  DURATION_ARRAY = 13,
}

struct PropertyDefThrift {
  1: string name
  2: PropertyType type
  3: bool is_required = false
}

enum DateTimeKind {
  DATE = 1,
  LOCAL_DATETIME = 2,
  DATETIME_WITH_TZ = 3,
}

enum TimeKind {
  LOCAL_TIME = 1,
  TIME_WITH_TZ = 2,
}

struct DateTimeValueThrift {
  1: DateTimeKind kind
  2: i64 year
  3: i64 month
  4: i64 day
  5: i64 hour
  6: i64 minute
  7: i64 second
  8: i64 nanos
  9: i32 tz_offset_min
  10: string tz_name
}

struct TimeValueThrift {
  1: TimeKind kind
  2: i64 hour
  3: i64 minute
  4: i64 second
  5: i64 nanos
  6: i32 tz_offset_min
  7: string tz_name
}

struct DurationValueThrift {
  1: i64 months
  2: i64 days
  3: i64 seconds
  4: i64 nanos
}

union PropertyValueThrift {
  1: bool bool_val
  2: i64 int_val
  3: double double_val
  4: string string_val
  5: list<i64> int_array
  6: list<double> double_array
  7: list<string> string_array
  8: DateTimeValueThrift datetime_val
  9: TimeValueThrift time_val
  10: DurationValueThrift duration_val
  11: list<DateTimeValueThrift> datetime_array
  12: list<TimeValueThrift> time_array
  13: list<DurationValueThrift> duration_array
}

struct LabelInfo {
  1: i16 id
  2: string name
  3: list<PropertyDefThrift> properties
  // 主键属性名，**有序**（顺序 = 主键元组顺序 = 唯一索引 accessor 顺序）；空 = 无主键
  4: list<string> pk_props
}

struct EdgeLabelInfo {
  1: i16 id
  2: string name
  3: list<PropertyDefThrift> properties
  4: bool directed = true
}

// ==================== Batch Import ====================

// 主键键值对：一个顶点/端点的主键（复合主键时多个，按声明顺序）
struct PkKey {
  1: string name
  2: PropertyValueThrift value
}

// 边端点引用：目标顶点的主标签 + 主键值。VertexId 是内部实现，不出现在写协议里。
struct PkRef {
  1: string primary_label
  2: list<PkKey> keys
}

struct VertexRecord {
  1: list<PropertyValueThrift> properties
  2: list<string> labels
  // 该顶点的主键；空 = 该顶点无主键（不可被边引用）
  3: list<PkKey> pk
}

struct EdgeRecord {
  1: PkRef src
  2: PkRef dst
  3: list<PropertyValueThrift> properties
}

struct BatchInsertVerticesResult {
  1: list<i64> vertex_ids
  2: i32 count
  3: i32 inserted
  // 因主键已存在（first-wins）或批内重复而未写入的记录数
  4: i32 duplicate_pk
}

struct BatchInsertEdgesResult {
  1: i32 inserted
  // 端点解析不到（对应现状 loader 侧的 skipped 计数）
  2: i32 skipped_unresolved
}

// ==================== Query Result ====================

union ResultValue {
  1: bool bool_val
  2: i64 int_val
  3: double double_val
  4: string string_val
  5: string vertex_json
  6: string edge_json
  7: string path_json
  8: string list_json
  9: string map_json
}

struct ResultRow {
  1: list<ResultValue> values
}

// Streaming response types
struct QueryStreamMeta {
  1: list<string> columns
}

struct ResultRowBatch {
  1: list<ResultRow> rows
}

// ==================== Graph Management ====================

struct GraphInfo {
  1: i32 graph_id
  2: string name
  3: i64 created_at
}

// ==================== Service ====================

service EuGraphService {
  // Graph management
  GraphInfo createGraph(1: string name)
  bool dropGraph(1: string name)
  list<GraphInfo> listGraphs()

  // DDL: Label management
  // pk_props 空列表 = 无主键（Thrift 参数不允许 optional，空即缺省）
  // merge_properties 非空 = 对已存在标签增量加属性（同名跳过、类型冲突报错）；
  // 用于「同一标签由多个数据文件供给」的场景（如 LDBC 的 Comment/Post 共享 Message 父标签）。
  LabelInfo createLabel(1: string name, 2: list<PropertyDefThrift> properties, 3: string graph_name,
                        4: list<string> pk_props, 5: list<PropertyDefThrift> merge_properties)
  list<LabelInfo> listLabels(1: string graph_name)

  // DDL: EdgeLabel management
  EdgeLabelInfo createEdgeLabel(1: string name, 2: list<PropertyDefThrift> properties, 3: string graph_name)
  list<EdgeLabelInfo> listEdgeLabels(1: string graph_name)

  // DML: Cypher query execution (streaming)
  QueryStreamMeta,stream<ResultRowBatch> executeCypher(1: string query, 2: string graph_name, 3: map<string, string> parameters)

  // Batch import
  BatchInsertVerticesResult batchInsertVertices(1: string label_name, 2: list<VertexRecord> records, 3: string graph_name)
  BatchInsertEdgesResult batchInsertEdges(1: string edge_label_name, 2: list<EdgeRecord> records, 3: string graph_name)
}
