"""Version-independent Neo4j driver probe used by the driver matrix test."""

import os
import time

from neo4j import GraphDatabase


def main():
    port = os.environ["EUGRAPH_BOLT_PORT"]
    node_id = int(time.time() * 1000)
    driver = GraphDatabase.driver(
        f"bolt://localhost:{port}",
        auth=("neo4j", "eugraph"),
    )

    try:
        with driver.session() as session:
            result = session.run(
                "CREATE (n:DriverMatrixNode {id: $id}) RETURN n",
                id=node_id,
            )
            record = result.single()
            assert record is not None
            assert record["n"] is not None

        with driver.session() as session:
            result = session.run(
                "MATCH (n:DriverMatrixNode {id: $id}) RETURN n.id AS id",
                id=node_id,
            )
            record = result.single()
            assert record is not None
            assert record["id"] == node_id
    finally:
        driver.close()


if __name__ == "__main__":
    main()
