namespace cpp2 eugraph.thrift

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
}

struct LabelInfo {
  1: i16 id
  2: string name
  3: list<PropertyDefThrift> properties
}

struct EdgeLabelInfo {
  1: i16 id
  2: string name
  3: list<PropertyDefThrift> properties
  4: bool directed = true
}

// ==================== Batch Import ====================

struct VertexRecord {
  1: list<PropertyValueThrift> properties
}

struct EdgeRecord {
  1: i64 src_vertex_id
  2: i64 dst_vertex_id
  3: list<PropertyValueThrift> properties
}

struct BatchInsertVerticesResult {
  1: list<i64> vertex_ids
  2: i32 count
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
  LabelInfo createLabel(1: string name, 2: list<PropertyDefThrift> properties, 3: string graph_name)
  list<LabelInfo> listLabels(1: string graph_name)

  // DDL: EdgeLabel management
  EdgeLabelInfo createEdgeLabel(1: string name, 2: list<PropertyDefThrift> properties, 3: string graph_name)
  list<EdgeLabelInfo> listEdgeLabels(1: string graph_name)

  // DML: Cypher query execution (streaming)
  QueryStreamMeta,stream<ResultRowBatch> executeCypher(1: string query, 2: string graph_name, 3: map<string, string> parameters)

  // Batch import
  BatchInsertVerticesResult batchInsertVertices(1: string label_name, 2: list<VertexRecord> records, 3: string graph_name)
  i32 batchInsertEdges(1: string edge_label_name, 2: list<EdgeRecord> records, 3: string graph_name)
}
