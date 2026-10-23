Feature: Correlated pattern comprehension inside reduce

  Background:
    Given an empty graph

  # complex-14 shape: the pattern comprehension in the reduce body depends on
  # the current relationship r (startNode/endNode). It must run once per list
  # element, not be treated as a clause-level comprehension.
  Scenario: Pattern comprehension in a reduce body is evaluated per element
    And having executed:
      """
      CREATE (a:Person {name: 'a'}), (b:Person {name: 'b'}),
             (a)<-[:HAS_CREATOR]-(:Comment {name: 'c'})-[:REPLY_OF]->
             (:Post {name: 'p'})-[:HAS_CREATOR]->(b),
             (a)-[:KNOWS]->(b)
      """
    When executing query:
      """
      MATCH p = (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})
      RETURN reduce(w = 0.0, r IN relationships(p) |
        w + size([(x:Person)<-[:HAS_CREATOR]-(:Comment)-[:REPLY_OF]->(:Post)
                  -[:HAS_CREATOR]->(y:Person)
                  WHERE (x.name = startNode(r).name AND y.name = endNode(r).name) OR
                        (x.name = endNode(r).name AND y.name = startNode(r).name)
                  | 1.0]) / 2.0) AS w
      """
    Then the result should be, in any order:
      | w   |
      | 0.5 |
    And no side effects

  Scenario: multiple correlated pattern comprehensions in one reduce body
    And having executed:
      """
      CREATE (a:Person {name: 'a'}), (b:Person {name: 'b'}),
             (a)<-[:HAS_CREATOR]-(:Comment {name: 'c'})-[:REPLY_OF]->
             (:Post {name: 'p'})-[:HAS_CREATOR]->(b),
             (a)-[:KNOWS]->(b), (b)-[:KNOWS]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH p = (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})
      RETURN reduce(w = 0, r IN relationships(p) |
        w + size([(x:Person)-[:KNOWS]->(:Person)
                  WHERE x.name = startNode(r).name | 1])
          + size([(x:Person)-[:KNOWS]->(:Person)
                  WHERE x.name = endNode(r).name | 1])) AS w
      """
    Then the result should be, in any order:
      | w |
      | 2 |
    And no side effects

  Scenario: a pattern comprehension can be the list source of a reduce
    And having executed:
      """
      CREATE (a:Person {name: 'pca'}), (b:Person {name: 'pcb'}),
             (a)<-[:HAS_CREATOR]-(:Comment {name: 'pcc'})-[:REPLY_OF]->
             (:Post {name: 'pcp'})-[:HAS_CREATOR]->(b),
             (a)-[:KNOWS]->(b)
      """
    When executing query:
      """
      MATCH (p:Person {name: 'pca'})-[r:KNOWS]->(q:Person {name: 'pcb'})
      WITH [r] AS rs
      RETURN [x IN rs | reduce(w = 0.0, v IN [
        (u:Person)<-[:HAS_CREATOR]-(:Comment)-[:REPLY_OF]->(:Post)
        -[:HAS_CREATOR]->(v:Person)
        WHERE (u.name = startNode(x).name AND v.name = endNode(x).name) OR
              (u.name = endNode(x).name AND v.name = startNode(x).name)
        | 1.0] | w + v)] AS w
      """
    Then the result should be, in any order:
      | w       |
      | [1.0]   |
    And no side effects
