import org.neo4j.driver.AuthTokens;
import org.neo4j.driver.Driver;
import org.neo4j.driver.GraphDatabase;
import org.neo4j.driver.Record;
import org.neo4j.driver.Result;
import org.neo4j.driver.Session;
import org.neo4j.driver.types.Node;
import org.neo4j.driver.types.Path;
import org.neo4j.driver.types.Relationship;

public class TestJavaDriver {
    public static void main(String[] args) {
        String uri = "bolt://127.0.0.1:" + System.getenv("EUGRAPH_BOLT_PORT");
        try (Driver driver = GraphDatabase.driver(uri, AuthTokens.basic("eugraph", "eugraph"))) {
            try (Session session = driver.session()) {
                session.run("CREATE (:JavaStart)-[:JAVA_EDGE {since: 2024}]->(:JavaEnd)").consume();

                Result result = session.run("MATCH p=(:JavaStart)-[:JAVA_EDGE]->(:JavaEnd) RETURN p");
                Record record = result.single();
                Path path = record.get("p").asPath();

                boolean startOk = false;
                for (String label : path.start().labels()) {
                    if (label.equals("JavaStart")) {
                        startOk = true;
                        break;
                    }
                }
                boolean endOk = false;
                for (String label : path.end().labels()) {
                    if (label.equals("JavaEnd")) {
                        endOk = true;
                        break;
                    }
                }
                if (!startOk || !endOk)
                    throw new AssertionError("path endpoints do not match expected labels");

                if (path.length() != 1)
                    throw new AssertionError("expected path length 1");
                Relationship rel = path.relationships().iterator().next();
                if (!rel.type().equals("JAVA_EDGE"))
                    throw new AssertionError("unexpected relationship type: " + rel.type());
                if (rel.get("since").asInt() != 2024)
                    throw new AssertionError("unexpected relationship property");

                Result relResult = session.run("MATCH ()-[r:JAVA_EDGE]->() RETURN r LIMIT 1");
                Relationship standalone = relResult.single().get("r").asRelationship();
                if (!standalone.type().equals("JAVA_EDGE"))
                    throw new AssertionError("unexpected standalone relationship type");
            }
        }
        System.out.println("Java driver path test passed");
    }
}
