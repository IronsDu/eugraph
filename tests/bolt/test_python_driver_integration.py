"""
Integration tests for EuGraph Bolt protocol support.

Uses the official neo4j Python driver to test Bolt protocol connectivity,
query execution, and type handling.

Prerequisites:
    pip install neo4j pytest

Usage:
    # Start EuGraph server first:
    eugraph-server --bolt-port 7687

    # Then run tests:
    pytest tests/bolt/test_python_driver_integration.py -v

    # Or with a custom port:
    EUGRAPH_BOLT_PORT=17687 pytest tests/bolt/test_python_driver_integration.py -v
"""

import os

import pytest

# Skip all tests if neo4j driver is not available
neo4j = pytest.importorskip("neo4j")


def get_bolt_url():
    port = os.environ.get("EUGRAPH_BOLT_PORT", "7687")
    return f"bolt://localhost:{port}"


TEST_DATABASE = "bolt_test"


def make_session(driver):
    return driver.session(database=TEST_DATABASE)


@pytest.fixture(scope="module", autouse=True)
def test_database():
    admin_driver = neo4j.GraphDatabase.driver(get_bolt_url())
    try:
        with admin_driver.session() as session:
            try:
                session.run(f"DROP DATABASE {TEST_DATABASE}")
            except Exception:
                pass
            session.run(f"CREATE DATABASE {TEST_DATABASE}")

        yield TEST_DATABASE

        with admin_driver.session() as session:
            session.run(f"DROP DATABASE {TEST_DATABASE}")
    finally:
        admin_driver.close()


# ---------------------------------------------------------------------------
# Connection tests
# ---------------------------------------------------------------------------


class TestConnection:
    """Test Bolt handshake and basic connectivity."""

    def test_verify_connectivity(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            driver.verify_connectivity()
        finally:
            driver.close()

    def test_session_hello(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN 1 AS n")
                records = list(result)
                assert len(records) == 1
                assert records[0]["n"] == 1
        finally:
            driver.close()


# ---------------------------------------------------------------------------
# CRUD tests
# ---------------------------------------------------------------------------


class TestCRUD:
    """Test basic CRUD operations via Cypher over Bolt."""

    def test_create_and_match_node(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                # Create a node
                session.run(
                    "CREATE (n:Person {name: $name, age: $age})",
                    name="Alice",
                    age=30,
                )
                # Match it back
                result = session.run(
                    "MATCH (n:Person {name: $name}) RETURN n.name, n.age",
                    name="Alice",
                )
                records = list(result)
                assert len(records) == 1
                assert records[0]["n.name"] == "Alice"
                assert records[0]["n.age"] == 30
        finally:
            driver.close()

    def test_create_and_match_edge(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                session.run(
                    "CREATE (a:Person {name: $a_name}) "
                    "CREATE (b:Person {name: $b_name}) "
                    "CREATE (a)-[:KNOWS {since: 2020}]->(b)",
                    a_name="Alice",
                    b_name="Bob",
                )
                result = session.run(
                    "MATCH (a:Person {name: $a_name})-[r:KNOWS]->(b:Person {name: $b_name}) "
                    "RETURN a.name, b.name, r.since",
                    a_name="Alice",
                    b_name="Bob",
                )
                records = list(result)
                assert len(records) == 1
                assert records[0]["a.name"] == "Alice"
                assert records[0]["b.name"] == "Bob"
                assert records[0]["r.since"] == 2020
        finally:
            driver.close()


# ---------------------------------------------------------------------------
# Protocol-version-specific wire compatibility
# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# Type handling tests
# ---------------------------------------------------------------------------


class TestTypes:
    """Test that various Cypher types round-trip correctly over Bolt."""

    def test_null_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN null AS v")
                records = list(result)
                assert len(records) == 1
                assert records[0]["v"] is None
        finally:
            driver.close()

    def test_string_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN 'hello' AS v")
                records = list(result)
                assert records[0]["v"] == "hello"
        finally:
            driver.close()

    def test_integer_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN 42 AS v")
                records = list(result)
                assert records[0]["v"] == 42
        finally:
            driver.close()

    def test_float_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN 3.14 AS v")
                records = list(result)
                assert records[0]["v"] == 3.14
        finally:
            driver.close()

    def test_boolean_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN true AS v")
                records = list(result)
                assert records[0]["v"] is True
        finally:
            driver.close()

    def test_list_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN [1, 2, 3] AS v")
                records = list(result)
                assert records[0]["v"] == [1, 2, 3]
        finally:
            driver.close()

    def test_map_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN {name: 'Alice', age: 30} AS v")
                records = list(result)
                assert records[0]["v"] == {"name": "Alice", "age": 30}
        finally:
            driver.close()

    def test_date_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN date('2025-01-15') AS v")
                records = list(result)
                assert len(records) == 1
                from neo4j.time import Date

                assert isinstance(records[0]["v"], Date)
                assert records[0]["v"].year == 2025
                assert records[0]["v"].month == 1
                assert records[0]["v"].day == 15
        finally:
            driver.close()

    def test_datetime_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run(
                    "RETURN datetime('2025-01-15T10:30:00+08:00') AS v"
                )
                records = list(result)
                assert len(records) == 1
                from neo4j.time import DateTime

                assert isinstance(records[0]["v"], DateTime)
        finally:
            driver.close()

    def test_time_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN time('10:30:00') AS v")
                records = list(result)
                assert len(records) == 1
                from neo4j.time import Time

                assert isinstance(records[0]["v"], Time)
        finally:
            driver.close()

    def test_duration_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run("RETURN duration('P1Y2M3D') AS v")
                records = list(result)
                assert len(records) == 1
                from neo4j.time import Duration

                assert isinstance(records[0]["v"], Duration)
                assert records[0]["v"].months == 14
                assert records[0]["v"].days == 3
        finally:
            driver.close()


# ---------------------------------------------------------------------------
# Parameterized query tests
# ---------------------------------------------------------------------------


class TestParameters:
    """Test parameter passing over Bolt."""

    def test_string_param(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run(
                    "RETURN $name AS v",
                    name="Alice",
                )
                records = list(result)
                assert records[0]["v"] == "Alice"
        finally:
            driver.close()

    def test_integer_param(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run(
                    "RETURN $n AS v",
                    n=42,
                )
                records = list(result)
                assert records[0]["v"] == 42
        finally:
            driver.close()

    def test_float_param(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run(
                    "RETURN $x AS v",
                    x=3.14,
                )
                records = list(result)
                assert records[0]["v"] == 3.14
        finally:
            driver.close()

    def test_list_param(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run(
                    "RETURN $items AS v",
                    items=[1, 2, 3],
                )
                records = list(result)
                assert records[0]["v"] == [1, 2, 3]
        finally:
            driver.close()

    def test_map_param(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                result = session.run(
                    "RETURN $props AS v",
                    props={"name": "Alice", "age": 30},
                )
                records = list(result)
                assert records[0]["v"] == {"name": "Alice", "age": 30}
        finally:
            driver.close()


# ---------------------------------------------------------------------------
# Built-in procedure tests
# ---------------------------------------------------------------------------


class TestProcedures:
    """Test Neo4j Browser compatibility procedures."""

    def test_db_labels_relationship_types_and_property_keys(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                session.run(
                    "CREATE (:ProcedureLabel {browser_key: 1})"
                    "-[:PROCEDURE_EDGE]->(:ProcedureLabel)"
                )

                labels = list(session.run("CALL db.labels()"))
                assert any(record["label"] == "ProcedureLabel" for record in labels)

                rel_types = list(session.run("CALL db.relationshipTypes()"))
                assert any(
                    record["relationshipType"] == "PROCEDURE_EDGE"
                    for record in rel_types
                )

                property_keys = list(session.run("CALL db.propertyKeys()"))
                assert any(
                    record["propertyKey"] == "browser_key"
                    for record in property_keys
                )
        finally:
            driver.close()

    def test_dbms_client_config_returns_name_value_rows(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                records = list(session.run("CALL dbms.clientConfig()"))
                assert records
                assert records[0].keys() == ["name", "value"]
                by_name = {record["name"]: record["value"] for record in records}
                assert by_name["dbms.security.auth_enabled"] is True
                assert by_name["browser.post_connect_cmd"] == ""
        finally:
            driver.close()

    def test_db_schema_visualization_returns_virtual_graph(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                session.run(
                    "CREATE (:SchemaLabel)-[:SCHEMA_REL]->(:SchemaLabel)"
                )

                records = list(session.run("CALL db.schema.visualization()"))
                assert records
                nodes = records[0]["nodes"]
                relationships = records[0]["relationships"]
                assert nodes
                assert relationships
                assert all(isinstance(node, neo4j.graph.Node) for node in nodes)
                assert all(
                    isinstance(rel, neo4j.graph.Relationship)
                    for rel in relationships
                )
                assert any("SchemaLabel" in node.labels for node in nodes)
                assert any(rel.type == "SCHEMA_REL" for rel in relationships)
        finally:
            driver.close()

    def test_dbms_procedures_lists_builtins(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                records = list(session.run("CALL dbms.procedures()"))
                names = {record["name"] for record in records}
                assert {
                    "db.ping",
                    "db.labels",
                    "db.relationshipTypes",
                    "db.propertyKeys",
                    "db.indexes",
                    "dbms.clientConfig",
                    "dbms.procedures",
                    "dbms.functions",
                } <= names
        finally:
            driver.close()

    def test_dbms_functions_lists_builtins(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                records = list(session.run("CALL dbms.functions()"))
                names = {record["name"] for record in records}
                assert "id" in names
                assert "abs" in names
                assert not any(name.startswith("__") for name in names)
        finally:
            driver.close()


# ---------------------------------------------------------------------------
# Transaction tests
# ---------------------------------------------------------------------------


class TestTransactions:
    """Test explicit transactions (BEGIN/COMMIT/ROLLBACK)."""

    def test_explicit_commit(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                tx = session.begin_transaction()
                tx.run("CREATE (n:TxTest {val: 1})")
                tx.commit()

                result = session.run(
                    "MATCH (n:TxTest {val: 1}) RETURN n.val"
                )
                records = list(result)
                assert len(records) == 1
                assert records[0]["n.val"] == 1
        finally:
            driver.close()

    def test_explicit_rollback(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with make_session(driver) as session:
                tx = session.begin_transaction()
                tx.run("CREATE (n:TxTest {val: 999})")
                tx.rollback()

                result = session.run(
                    "MATCH (n:TxTest {val: 999}) RETURN count(n) AS cnt"
                )
                records = list(result)
                assert records[0]["cnt"] == 0
        finally:
            driver.close()


# ---------------------------------------------------------------------------
# Main (for running without pytest)
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
