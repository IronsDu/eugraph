#!/usr/bin/env python3
"""Benchmark the 8 LDBC interactive update statements against one Bolt endpoint.

The ``interactive-update-*.cypher`` files carry no ``:param`` block: the LDBC
update stream supplies the parameters at replay time. This script synthesizes
valid parameters from the loaded graph instead:

* CREATE statements get fresh ids (``--id-base`` + a process-wide counter);
* like / membership / friendship first pick a person and target that do not
  already have the relationship, so every execution is a real insert;
* ids are sent as ints or strings depending on the endpoint (eugraph stores
  int64 ids, neo4j stores string ids), avoiding "0 rows -> fake speed";
* on a string-id store, update-7's ``$replyToPostId + $replyToCommentId + 1``
  would concatenate strings and match nothing, so the computed id is passed as
  ``$messageId`` instead (the numeric-id side keeps the query file verbatim);
* every statement is effect-verified (new node/edge must exist). A no-op raises
  instead of being reported as a fast update.

Each statement runs ``--runs`` times (default 3); the reported value is the min
and median wall time including the commit.  This is intentionally a
single-engine tool: interleave two invocations for A/B numbers.

Usage::

    scripts/bench_ldbc_updates.py --queries-dir <dir> --uri bolt://127.0.0.1:17688
    scripts/bench_ldbc_updates.py --queries-dir <dir> --uri bolt://127.0.0.1:7687 \
        --user neo4j --password neo4j --database neo4j --json
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from pathlib import Path

try:
    from neo4j import GraphDatabase
except ImportError:  # pragma: no cover
    sys.exit("needs the 'neo4j' Python driver")

ID_BASE = 8_000_000_000_000
_next_id = ID_BASE


def alloc_id() -> int:
    global _next_id
    _next_id += 1
    return _next_id


def query(n: int) -> str:
    return (Path(_args.queries_dir) / f"interactive-update-{n}.cypher").read_text()


def fmt(value):
    if _id_type == "str":
        if isinstance(value, list):
            return [fmt(v) for v in value]
        return str(value)
    return value


def run(session, text: str, params: dict) -> tuple[float, int]:
    t0 = time.perf_counter()
    summary = session.run(text, **params).consume()
    return (time.perf_counter() - t0) * 1000, summary.counters.nodes_created


def fresh_id(session, label: str) -> int:
    """Allocate an id not present on `label` (safe across repeated runs)."""
    while True:
        candidate = alloc_id()
        hit = session.run(f"MATCH (n:{label} {{id: $id}}) RETURN 1 LIMIT 1", id=fmt(candidate)).single()
        if hit is None:
            return candidate


def first(session, label: str, limit: int = 1):
    rows = session.run(f"MATCH (n:{label}) RETURN n.id AS id LIMIT {limit}").data()
    return [r["id"] for r in rows]


def pick(session, text: str, n: int, **params):
    return [r[list(r)[0]] for r in session.run(text, **params).data()][:n]


def main() -> int:
    global _id_type
    parser = argparse.ArgumentParser()
    parser.add_argument("--queries-dir", required=True)
    parser.add_argument("--uri", default="bolt://127.0.0.1:17688")
    parser.add_argument("--user", default=None)
    parser.add_argument("--password", default=None)
    parser.add_argument("--database", default=None)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--id-base", type=int, default=ID_BASE)
    parser.add_argument("--json", action="store_true")
    global _args
    _args = parser.parse_args()
    global _next_id
    _next_id = _args.id_base

    auth = (_args.user, _args.password) if _args.user else None
    drv = GraphDatabase.driver(_args.uri, auth=auth)
    result: dict[str, list[float]] = {}
    with drv.session(database=_args.database) as session:
        probe = session.run("MATCH (n:Person) RETURN n.id AS id LIMIT 1").single()
        if probe is None:
            sys.exit("no Person rows: wrong graph or empty database")
        _id_type = "str" if isinstance(probe["id"], str) else "int"

        city = first(session, "City")[0]
        country = first(session, "Country")[0]
        tags = first(session, "Tag", 4)
        orgs = first(session, "Organisation", 2)
        person = first(session, "Person")[0]
        forum = first(session, "Forum")[0]
        post = session.run(
            "MATCH (p:Post)<-[:CONTAINER_OF]-(f:Forum) MATCH (p)-[:IS_LOCATED_IN]->(c:Country) "
            "RETURN p.id AS post, f.id AS forum, c.id AS country LIMIT 1"
        ).data()[0]
        comment = first(session, "Comment")[0]

        def verify_count(cypher: str, params: dict) -> bool:
            row = session.run(cypher, **params).single()
            return row is not None and row["n"] > 0

        def measure(n: int, text: str, params_list: list[dict], verify_cypher: str) -> None:
            times = []
            for params in params_list:
                ms, _ = run(session, text, params)
                if not verify_count(verify_cypher, params):
                    raise RuntimeError(f"update-{n}: statement had no effect "
                                       f"(params={params}); refusing to report a no-op time")
                times.append(ms)
            result[f"update-{n}"] = times

        # 1: add person
        base = {
            "cityId": fmt(city), "personFirstName": "Perf", "personLastName": "Update",
            "gender": "female", "birthday": 365126400000, "creationDate": 1290926804528,
            "locationIP": "10.0.0.1", "browserUsed": "Chrome", "languages": ["en"],
            "emails": ["perf.update@example.com"], "tagIds": [fmt(t) for t in tags],
            "studyAt": [[fmt(orgs[0]), 2000]], "workAt": [[fmt(orgs[1]), 2001]],
        }
        measure(1, query(1), [{**base, "personId": fmt(fresh_id(session, "Person"))} for _ in range(_args.runs)],
                "MATCH (p:Person {id: $personId}) RETURN count(p) AS n")

        # 2 / 3: like a post / comment (pick likers without that like)
        for n, label, target in ((2, "Post", post["post"]), (3, "Comment", comment)):
            likers = pick(
                session,
                f"MATCH (p:Person) WHERE NOT (p)-[:LIKES]->(:{label} {{id: $id}}) "
                f"RETURN p.id AS id LIMIT {_args.runs}",
                _args.runs,
                id=fmt(target),
            )
            text = query(n)
            key = "postId" if n == 2 else "commentId"
            measure(n, text, [
                {"personId": fmt(p), key: fmt(target), "creationDate": 1291000000000 + i}
                for i, p in enumerate(likers)
            ], f"MATCH (:Person {{id: $personId}})-[r:LIKES]->(:{label} {{id: ${key}}}) RETURN count(r) AS n")

        # 4: add forum
        fbase = {"moderatorPersonId": fmt(person), "forumTitle": "Perf Forum",
                 "creationDate": 1291000000000, "tagIds": [fmt(t) for t in tags[:2]]}
        measure(4, query(4), [{**fbase, "forumId": fmt(fresh_id(session, "Forum"))} for _ in range(_args.runs)],
                "MATCH (f:Forum {id: $forumId}) RETURN count(f) AS n")

        # 5: add forum membership
        members = pick(
            session,
            "MATCH (f:Forum {id: $fid}), (p:Person) WHERE NOT (f)-[:HAS_MEMBER]->(p) "
            f"RETURN p.id AS id LIMIT {_args.runs}",
            _args.runs,
            fid=fmt(forum),
        )
        measure(5, query(5), [
            {"forumId": fmt(forum), "personId": fmt(p), "joinDate": 1291000000000 + i}
            for i, p in enumerate(members)
        ], "MATCH (:Forum {id: $forumId})-[r:HAS_MEMBER]->(:Person {id: $personId}) RETURN count(r) AS n")

        # 6: add post
        pbase = {"authorPersonId": fmt(person), "countryId": fmt(post["country"]),
                 "forumId": fmt(post["forum"]), "creationDate": 1291000000000,
                 "locationIP": "10.0.0.2", "browserUsed": "Chrome", "language": "en",
                 "content": "perf post", "imageFile": "", "length": 10,
                 "tagIds": [fmt(t) for t in tags[:2]]}
        measure(6, query(6), [{**pbase, "postId": fmt(fresh_id(session, "Post"))} for _ in range(_args.runs)],
                "MATCH (p:Post {id: $postId}) RETURN count(p) AS n")

        # 7: add comment (reply to a post; replyToCommentId = -1)
        cbase = {"authorPersonId": fmt(person), "countryId": fmt(post["country"]),
                 "creationDate": 1291000000000, "locationIP": "10.0.0.3",
                 "browserUsed": "Chrome", "content": "perf comment", "length": 12,
                 "tagIds": [fmt(t) for t in tags[:2]]}
        update7 = query(7)
        if _id_type == "str":
            # Neo4j stores ids as strings: `$a + $b + 1` concatenates instead of
            # doing arithmetic, so the file as written matches nothing. Substitute
            # the computed id to measure the real insert on this store.
            update7 = update7.replace("$replyToPostId + $replyToCommentId + 1", "$messageId")
            cbase["messageId"] = fmt(post["post"])
        else:
            cbase["replyToPostId"] = fmt(post["post"])
            cbase["replyToCommentId"] = -1
        measure(7, update7, [{**cbase, "commentId": fmt(fresh_id(session, "Comment"))} for _ in range(_args.runs)],
                "MATCH (c:Comment {id: $commentId}) RETURN count(c) AS n")

        # 8: add friendship
        friends = pick(
            session,
            "MATCH (a:Person {id: $aid}), (b:Person) WHERE b <> a AND NOT (a)-[:KNOWS]->(b) "
            f"RETURN b.id AS id LIMIT {_args.runs}",
            _args.runs,
            aid=fmt(person),
        )
        measure(8, query(8), [
            {"person1Id": fmt(person), "person2Id": fmt(b), "creationDate": 1291000000000 + i}
            for i, b in enumerate(friends)
        ], "MATCH (:Person {id: $person1Id})-[r:KNOWS]->(:Person {id: $person2Id}) RETURN count(r) AS n")
    drv.close()

    if _args.json:
        print(json.dumps({k: min(v) for k, v in result.items()}, indent=1, sort_keys=True))
    else:
        print(f"{'stmt':10s} {'min(ms)':>9s} {'median(ms)':>11s}")
        for name in sorted(result):
            print(f"{name:10s} {min(result[name]):9.2f} {statistics.median(result[name]):11.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
