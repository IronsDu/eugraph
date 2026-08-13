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
    pytest tests/bolt/test_bolt_integration.py -v

    # Or with a custom port:
    EUGRAPH_BOLT_PORT=17687 pytest tests/bolt/test_bolt_integration.py -v
"""

import os
import socket
import struct
import sys
import time

import pytest

# Skip all tests if neo4j driver is not available
neo4j = pytest.importorskip("neo4j")


def get_bolt_url():
    port = os.environ.get("EUGRAPH_BOLT_PORT", "7687")
    return f"bolt://localhost:{port}"


# ---------------------------------------------------------------------------
# Minimal raw Bolt client for protocol-version-specific assertions.
# The official Python driver negotiates only its preferred protocol version,
# so these helpers explicitly negotiate a requested version to test wire
# compatibility for both Bolt 4.x and Bolt 5.x.
# ---------------------------------------------------------------------------

BOLT_MAGIC = 0x6060B017


def _pack_str(value):
    data = value.encode("utf-8")
    size = len(data)
    if size < 16:
        return bytes([0x80 | size]) + data
    if size < 256:
        return b"\xd0" + bytes([size]) + data
    return b"\xd1" + struct.pack(">H", size) + data


def _pack_int(value):
    if 0 <= value <= 127:
        return bytes([value])
    if -16 <= value < 0:
        return bytes([value & 0xFF])
    if -128 <= value < 128:
        return b"\xc8" + struct.pack(">b", value)
    return b"\xca" + struct.pack(">i", value)


def _pack_value(value):
    if isinstance(value, str):
        return _pack_str(value)
    if isinstance(value, int):
        return _pack_int(value)
    if isinstance(value, dict):
        out = bytes([0xA0 | len(value)])
        for key, item in value.items():
            out += _pack_str(key) + _pack_value(item)
        return out
    if isinstance(value, (list, tuple)):
        out = bytes([0x90 | len(value)])
        for item in value:
            out += _pack_value(item)
        return out
    raise TypeError(f"unsupported raw Bolt test value: {type(value)!r}")


def _pack_struct(tag, fields):
    return bytes([0xB0 | len(fields), tag]) + b"".join(_pack_value(f) for f in fields)


def _chunk(message):
    return struct.pack(">H", len(message)) + message + b"\x00\x00"


def _recv_exact(sock, size):
    chunks = []
    remaining = size
    while remaining:
        data = sock.recv(remaining)
        if not data:
            raise AssertionError("unexpected EOF while reading Bolt response")
        chunks.append(data)
        remaining -= len(data)
    return b"".join(chunks)


def _recv_next_message(sock):
    body = bytearray()
    while True:
        header = _recv_exact(sock, 2)
        chunk_size = (header[0] << 8) | header[1]
        if chunk_size == 0:
            return bytes(body)
        if chunk_size > 0x3FFF:
            raise AssertionError(f"invalid Bolt chunk size: {chunk_size}")
        body.extend(_recv_exact(sock, chunk_size))


def _raw_bolt_connect(bolt_port, protocol_version):
    sock = socket.create_connection(("127.0.0.1", bolt_port), timeout=5)
    sock.settimeout(5)

    proposals = [protocol_version, 0x00000000, 0x00000000, 0x00000000]
    handshake = struct.pack(">I", BOLT_MAGIC) + b"".join(
        struct.pack(">I", proposal) for proposal in proposals
    )
    sock.sendall(handshake)
    negotiated = _recv_exact(sock, 4)

    hello = _pack_struct(
        0x01,
        [
            {
                "user_agent": f"eugraph-raw-bolt-test/{protocol_version >> 8}.{protocol_version & 0xFF}",
                "scheme": "basic",
                "principal": "neo4j",
                "credentials": "eugraph",
            }
        ],
    )
    sock.sendall(_chunk(hello))
    _recv_next_message(sock)

    return sock, negotiated


def _raw_bolt_create_node(bolt_port, protocol_version, query):
    sock, negotiated = _raw_bolt_connect(bolt_port, protocol_version)
    try:
        run = _pack_struct(0x10, [query, {}, {}])
        sock.sendall(_chunk(run))
        _recv_next_message(sock)

        pull = _pack_struct(0x3F, [{"n": -1}])
        sock.sendall(_chunk(pull))
        record = _recv_next_message(sock)
        _recv_next_message(sock)  # SUCCESS
        return negotiated, record
    finally:
        sock.close()


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
            with driver.session() as session:
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
            with driver.session() as session:
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
            with driver.session() as session:
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


class TestBoltVersionSpecificEncoding:
    """Exercise struct shapes that differ between Bolt 4.x and Bolt 5.x."""

    def test_node_struct_is_v4_compatible(self):
        port = int(os.environ.get("EUGRAPH_BOLT_PORT", "7687"))
        negotiated, record = _raw_bolt_create_node(
            port,
            0x00000404,
            "CREATE (n:Person {name: 'Alice', age: 30}) RETURN n",
        )
        assert negotiated == bytes([0x00, 0x00, 0x04, 0x04])
        # Bolt 4.x Node = 3 fields: id, labels, properties.
        assert b"\xb3\x4e" in record
        assert b"\xb4\x4e" not in record

    def test_node_struct_is_v5_compatible(self):
        port = int(os.environ.get("EUGRAPH_BOLT_PORT", "7687"))
        negotiated, record = _raw_bolt_create_node(
            port,
            0x00000501,
            "CREATE (n:Person {name: 'Alice', age: 30}) RETURN n",
        )
        assert negotiated == bytes([0x00, 0x00, 0x01, 0x05])
        # Bolt 5.x Node = 4 fields: id, labels, properties, element_id.
        assert b"\xb4\x4e" in record

    def test_relationship_struct_is_v4_compatible(self):
        port = int(os.environ.get("EUGRAPH_BOLT_PORT", "7687"))
        query = (
            "CREATE (a:Person {name: 'Alice'}) "
            "CREATE (b:Person {name: 'Bob'}) "
            "CREATE (a)-[r:KNOWS {since: 2024}]->(b) "
            "RETURN a, b, r"
        )
        negotiated, record = _raw_bolt_create_node(port, 0x00000404, query)
        assert negotiated == bytes([0x00, 0x00, 0x04, 0x04])
        # Bolt 4.x Relationship = 5 fields:
        # id, start, end, type, properties.
        assert b"\xb5\x52" in record


# ---------------------------------------------------------------------------
# Type handling tests
# ---------------------------------------------------------------------------


class TestTypes:
    """Test that various Cypher types round-trip correctly over Bolt."""

    def test_null_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with driver.session() as session:
                result = session.run("RETURN null AS v")
                records = list(result)
                assert len(records) == 1
                assert records[0]["v"] is None
        finally:
            driver.close()

    def test_string_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with driver.session() as session:
                result = session.run("RETURN 'hello' AS v")
                records = list(result)
                assert records[0]["v"] == "hello"
        finally:
            driver.close()

    def test_integer_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with driver.session() as session:
                result = session.run("RETURN 42 AS v")
                records = list(result)
                assert records[0]["v"] == 42
        finally:
            driver.close()

    def test_float_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with driver.session() as session:
                result = session.run("RETURN 3.14 AS v")
                records = list(result)
                assert records[0]["v"] == 3.14
        finally:
            driver.close()

    def test_boolean_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with driver.session() as session:
                result = session.run("RETURN true AS v")
                records = list(result)
                assert records[0]["v"] is True
        finally:
            driver.close()

    def test_list_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with driver.session() as session:
                result = session.run("RETURN [1, 2, 3] AS v")
                records = list(result)
                assert records[0]["v"] == [1, 2, 3]
        finally:
            driver.close()

    def test_map_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with driver.session() as session:
                result = session.run("RETURN {name: 'Alice', age: 30} AS v")
                records = list(result)
                assert records[0]["v"] == {"name": "Alice", "age": 30}
        finally:
            driver.close()

    def test_date_return(self):
        driver = neo4j.GraphDatabase.driver(get_bolt_url())
        try:
            with driver.session() as session:
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
            with driver.session() as session:
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
            with driver.session() as session:
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
            with driver.session() as session:
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
            with driver.session() as session:
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
            with driver.session() as session:
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
            with driver.session() as session:
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
            with driver.session() as session:
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
            with driver.session() as session:
                result = session.run(
                    "RETURN $props AS v",
                    props={"name": "Alice", "age": 30},
                )
                records = list(result)
                assert records[0]["v"] == {"name": "Alice", "age": 30}
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
            with driver.session() as session:
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
            with driver.session() as session:
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
