Feature: reduce expression basics

  Background:
    Given an empty graph

  Scenario: reduce sums a non-empty list
    When executing query:
      """
      RETURN reduce(w = 0, x IN [1, 2, 3] | w + x) AS s
      """
    Then the result should be, in any order:
      | s |
      | 6 |
    And no side effects

  Scenario: reduce over an empty list returns the initial accumulator
    When executing query:
      """
      RETURN reduce(w = 42, x IN [] | w + x) AS s
      """
    Then the result should be, in any order:
      | s  |
      | 42 |
    And no side effects

  Scenario: reduce over a null list returns null
    When executing query:
      """
      RETURN reduce(w = 42, x IN null | w + x) AS s
      """
    Then the result should be, in any order:
      | s    |
      | null |
    And no side effects

  Scenario: a null accumulator propagates to the result
    When executing query:
      """
      RETURN reduce(w = null, x IN [1, 2] | w + x) AS s
      """
    Then the result should be, in any order:
      | s    |
      | null |
    And no side effects

  Scenario: a null element propagates to the result
    When executing query:
      """
      RETURN reduce(w = 0, x IN [1, null, 2] | w + x) AS s
      """
    Then the result should be, in any order:
      | s    |
      | null |
    And no side effects

  Scenario: nested reduce expressions
    When executing query:
      """
      RETURN reduce(w = 0, x IN [1, 2] | w + reduce(v = 0, y IN [1, 2] | v + y)) AS s
      """
    Then the result should be, in any order:
      | s |
      | 6 |
    And no side effects

  Scenario: multiple reduce expressions in one result row
    When executing query:
      """
      RETURN reduce(w = 0, x IN [1, 2] | w + x) AS sum,
             reduce(w = 1, x IN [1, 2] | w * x) AS product
      """
    Then the result should be, in any order:
      | sum | product |
      | 3   | 2       |
    And no side effects

  Scenario: reduce over the nodes of a path
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})-[:KNOWS]->(c:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH p = (a:Person {name: 'a'})-[:KNOWS*2..2]->(c:Person {name: 'c'})
      RETURN reduce(n = 0, x IN nodes(p) | n + 1) AS n
      """
    Then the result should be, in any order:
      | n |
      | 3 |
    And no side effects

  Scenario: reduce over the relationships of a path
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})-[:KNOWS]->(c:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH p = (a:Person {name: 'a'})-[:KNOWS*2..2]->(c:Person {name: 'c'})
      RETURN reduce(n = 0, r IN relationships(p) | n + 1) AS n
      """
    Then the result should be, in any order:
      | n |
      | 2 |
    And no side effects
