import java.time.LocalDate;
import java.time.LocalTime;
import java.time.OffsetDateTime;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.neo4j.driver.AuthTokens;
import org.neo4j.driver.Driver;
import org.neo4j.driver.GraphDatabase;
import org.neo4j.driver.Record;
import org.neo4j.driver.Result;
import org.neo4j.driver.Session;
import org.neo4j.driver.Value;
import org.neo4j.driver.types.IsoDuration;
import org.neo4j.driver.types.Node;
import org.neo4j.driver.types.Path;
import org.neo4j.driver.types.Relationship;

public class TestJavaDriver {
    private static void check(boolean condition, String message) {
        if (!condition)
            throw new AssertionError(message);
    }

    private static void testConnection(Session session) {
        check(session.run("RETURN 1 AS n").single().get("n").asInt() == 1, "hello failed");
    }

    private static void testCrud(Session session) {
        session.run("CREATE (n:JavaPerson {name: 'Alice', age: 30})").consume();
        Record rec = session.run("MATCH (n:JavaPerson {name: 'Alice'}) RETURN n.name AS name, n.age AS age").single();
        check(rec.get("name").asString().equals("Alice"), "node name roundtrip failed");
        check(rec.get("age").asInt() == 30, "node age roundtrip failed");

        session.run("MATCH (n:JavaPerson) DETACH DELETE n").consume();
        session.run(
                "CREATE (a:JavaPerson {name: 'Alice'}) "
                        + "CREATE (b:JavaPerson {name: 'Bob'}) "
                        + "CREATE (a)-[:JAVA_KNOWS {since: 2020}]->(b)").consume();
        rec = session.run(
                "MATCH (a:JavaPerson {name: 'Alice'})-[r:JAVA_KNOWS]->(b:JavaPerson {name: 'Bob'}) "
                        + "RETURN a.name AS a, b.name AS b, r.since AS since").single();
        check(rec.get("a").asString().equals("Alice"), "edge start roundtrip failed");
        check(rec.get("b").asString().equals("Bob"), "edge end roundtrip failed");
        check(rec.get("since").asInt() == 2020, "edge property roundtrip failed");
        session.run("MATCH (n:JavaPerson) DETACH DELETE n").consume();
    }

    private static void testTypes(Session session) {
        check(session.run("RETURN null AS v").single().get("v").isNull(), "null failed");
        check(session.run("RETURN 'hello' AS v").single().get("v").asString().equals("hello"), "string failed");
        check(session.run("RETURN 42 AS v").single().get("v").asInt() == 42, "integer failed");
        check(session.run("RETURN 3.14 AS v").single().get("v").asDouble() == 3.14, "float failed");
        check(session.run("RETURN true AS v").single().get("v").asBoolean(), "boolean failed");
        List<Integer> list = session.run("RETURN [1, 2, 3] AS v").single().get("v").asList(Value::asInt);
        check(list.equals(List.of(1, 2, 3)), "list failed");
        Map<String, Object> map = session.run("RETURN {name: 'Alice', age: 30} AS v").single().get("v")
                .asMap(Value::asObject);
        check(map.get("name").equals("Alice") && map.get("age").equals(30L), "map failed");

        check(session.run("RETURN date('2025-01-15') AS v").single().get("v").asLocalDate()
                .equals(LocalDate.of(2025, 1, 15)), "date failed");
        check(session.run("RETURN time('10:30:00') AS v").single().get("v").asOffsetTime()
                .toLocalTime().equals(LocalTime.of(10, 30, 0)), "time failed");
        check(session.run("RETURN datetime('2025-01-15T10:30:00+08:00') AS v").single().get("v")
                .asOffsetDateTime().toLocalDate().equals(LocalDate.of(2025, 1, 15)), "datetime failed");
        IsoDuration duration = session.run("RETURN duration('P1Y2M3D') AS v").single().get("v").asIsoDuration();
        check(duration.months() == 14 && duration.days() == 3, "duration failed");
    }

    private static void testPath(Session session) {
        session.run("CREATE (:JavaPathStart)-[:JAVA_PATH_EDGE {since: 2024}]->(:JavaPathEnd)").consume();
        Record rec = session.run("MATCH p=(:JavaPathStart)-[:JAVA_PATH_EDGE]->(:JavaPathEnd) RETURN p").single();
        Path path = rec.get("p").asPath();
        check(path.length() == 1, "path length failed");
        boolean startOk = false;
        for (String label : path.start().labels()) {
            if (label.equals("JavaPathStart"))
                startOk = true;
        }
        boolean endOk = false;
        for (String label : path.end().labels()) {
            if (label.equals("JavaPathEnd"))
                endOk = true;
        }
        check(startOk && endOk, "path endpoint labels failed");
        Relationship rel = path.relationships().iterator().next();
        check(rel.type().equals("JAVA_PATH_EDGE"), "path relationship type failed");
        check(rel.get("since").asInt() == 2024, "path relationship property failed");

        Relationship standalone = session.run("MATCH ()-[r:JAVA_PATH_EDGE]->() RETURN r LIMIT 1").single()
                .get("r").asRelationship();
        check(standalone.type().equals("JAVA_PATH_EDGE"), "standalone relationship failed");
        session.run("MATCH (n:JavaPathStart) DETACH DELETE n").consume();
        session.run("MATCH (n:JavaPathEnd) DETACH DELETE n").consume();
    }

    private static void testParameters(Session session) {
        check(session.run("RETURN $name AS v", Map.of("name", "Alice")).single().get("v").asString()
                .equals("Alice"), "string param failed");
        check(session.run("RETURN $n AS v", Map.of("n", 42L)).single().get("v").asLong() == 42L, "integer param failed");
        check(session.run("RETURN $x AS v", Map.of("x", 3.14)).single().get("v").asDouble() == 3.14, "float param failed");
        List<Integer> list = session.run("RETURN $items AS v", Map.of("items", List.of(1L, 2L, 3L))).single()
                .get("v").asList(Value::asInt);
        check(list.equals(List.of(1, 2, 3)), "list param failed");
        Map<String, Object> props = new HashMap<>();
        props.put("name", "Alice");
        props.put("age", 30L);
        Map<String, Object> map = session.run("RETURN $props AS v", Map.of("props", props)).single().get("v")
                .asMap(Value::asObject);
        check(map.get("name").equals("Alice") && map.get("age").equals(30L), "map param failed");
    }

    private static void testProcedures(Session session) {
        session.run("CREATE (:JavaProcLabel {browser_key: 1})-[:JAVA_PROC_EDGE]->(:JavaProcLabel)").consume();

        Set<String> labels = new HashSet<>();
        for (Record rec : session.run("CALL db.labels()").list())
            labels.add(rec.get("label").asString());
        check(labels.contains("JavaProcLabel"), "db.labels failed");

        Set<String> types = new HashSet<>();
        for (Record rec : session.run("CALL db.relationshipTypes()").list())
            types.add(rec.get("relationshipType").asString());
        check(types.contains("JAVA_PROC_EDGE"), "db.relationshipTypes failed");

        Set<String> keys = new HashSet<>();
        for (Record rec : session.run("CALL db.propertyKeys()").list())
            keys.add(rec.get("propertyKey").asString());
        check(keys.contains("browser_key"), "db.propertyKeys failed");

        Record config = session.run("CALL dbms.clientConfig()").list().get(0);
        check(config.keys().contains("name") && config.keys().contains("value"), "dbms.clientConfig columns failed");

        Record vis = session.run("CALL db.schema.visualization()").single();
        List<Node> nodes = vis.get("nodes").asList(Value::asNode);
        List<Relationship> rels = vis.get("relationships").asList(Value::asRelationship);
        check(!nodes.isEmpty() && !rels.isEmpty(), "db.schema.visualization empty");
        boolean foundLabel = false;
        for (Node node : nodes) {
            for (String label : node.labels()) {
                if (label.equals("JavaProcLabel"))
                    foundLabel = true;
            }
        }
        boolean foundType = false;
        for (Relationship relationship : rels) {
            if (relationship.type().equals("JAVA_PROC_EDGE"))
                foundType = true;
        }
        check(foundLabel && foundType, "db.schema.visualization content failed");
        session.run("MATCH (n:JavaProcLabel) DETACH DELETE n").consume();
    }

    private static void testTransactions(Session session) {
        session.run("CREATE (:JavaTx {n: 1})").consume();
        try (org.neo4j.driver.Transaction tx = session.beginTransaction()) {
            tx.run("CREATE (:JavaTx {n: 2})").consume();
            tx.rollback();
        }
        long count = session.run("MATCH (n:JavaTx) RETURN count(n) AS c").single().get("c").asLong();
        check(count == 1, "rollback failed");
        session.run("MATCH (n:JavaTx) DETACH DELETE n").consume();
    }

    public static void main(String[] args) {
        String uri = "bolt://127.0.0.1:" + System.getenv("EUGRAPH_BOLT_PORT");
        try (Driver driver = GraphDatabase.driver(uri, AuthTokens.basic("eugraph", "eugraph"));
                Session session = driver.session()) {
            testConnection(session);
            testCrud(session);
            testTypes(session);
            testPath(session);
            testParameters(session);
            testProcedures(session);
            testTransactions(session);
        }
        System.out.println("Java driver integration test passed");
    }
}
