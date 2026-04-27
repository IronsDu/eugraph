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
}

struct PropertyDefThrift {
  1: string name
  2: PropertyType type
  3: bool is_required = false
}

union PropertyValueThrift {
  1: bool bool_val
  2: i64 int_val
  3: double double_val
  4: string string_val
  5: list<i64> int_array
  6: list<double> double_array
  7: list<string> string_array
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
  1: PropertyValueThrift pk_value
  2: list<PropertyValueThrift> properties
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
}

struct ResultRow {
  1: list<ResultValue> values
}

struct QueryResult {
  1: list<string> columns
  2: list<ResultRow> rows
  3: i64 rows_affected = 0
  4: string error
}

// ==================== Service ====================

service EuGraphService {
  // DDL: Label management
  LabelInfo createLabel(1: string name, 2: list<PropertyDefThrift> properties)
  list<LabelInfo> listLabels()

  // DDL: EdgeLabel management
  EdgeLabelInfo createEdgeLabel(1: string name, 2: list<PropertyDefThrift> properties)
  list<EdgeLabelInfo> listEdgeLabels()

  // DML: Cypher query execution
  QueryResult executeCypher(1: string query)

  // Batch import
  BatchInsertVerticesResult batchInsertVertices(1: string label_name, 2: list<VertexRecord> records)
  i32 batchInsertEdges(1: string edge_label_name, 2: list<EdgeRecord> records)
}
