Feature: Aggregate typed-path regressions

  # Regression cases for the typed aggregate pipeline.  The pipeline stores
  # arguments in per-chunk columns; these scenarios pin the value semantics that
  # must survive that representation change:
  #
  #   - graph entities keep their identity across topology/semantic stages;
  #   - NULL is skipped by sum/min/max/collect for normal aggregates;
  #   - mixed int/double values keep their runtime type for min/max;
  #   - pattern/list comprehension still keeps a NULL projection (the internal
  #     collect is explicitly marked keeps_nulls).

  Scenario: count(DISTINCT vertex) agrees with WITH DISTINCT on an undirected pattern
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})
      CREATE (a)-[:KNOWS]->(c:Person {name: 'c'})
      CREATE (b)-[:KNOWS]->(c)
      """
    When executing query:
      """
      MATCH (n:Person)-[:KNOWS]-(m)
      RETURN count(DISTINCT m) AS c
      """
    Then the result should be, in any order:
      | c |
      | 3 |
    When executing query:
      """
      MATCH (n:Person)-[:KNOWS]-(m)
      WITH DISTINCT m
      RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 3 |

  Scenario: sum over a dynamic property skips missing values
    Given an empty graph
    And having executed:
      """
      CREATE ({name: 'a', num: 33})
      CREATE ({name: 'a'})
      CREATE ({name: 'a', num: 42})
      """
    When executing query:
      """
      MATCH (n)
      RETURN n.name AS name, sum(n.num) AS total
      """
    Then the result should be, in any order:
      | name | total |
      | 'a'  | 75    |

  Scenario: max and min preserve the selected mixed numeric type
    Given an empty graph
    When executing query:
      """
      UNWIND [1, 2.0, 5, null, 3.2, 0.1] AS x
      RETURN max(x) AS mx, min(x) AS mn
      """
    Then the result should be, in any order:
      | mx | mn  |
      | 5  | 0.1 |

  Scenario: collect filters NULL for a normal aggregate
    Given an empty graph
    When executing query:
      """
      UNWIND [1, null, 3] AS x
      RETURN collect(x) AS xs
      """
    Then the result should be, in any order:
      | xs    |
      | [1, 3] |

  Scenario: list comprehension keeps a NULL projection
    Given an empty graph
    When executing query:
      """
      UNWIND [1, null, 3] AS x
      RETURN x, [y IN [x] | y] AS ys
      """
    Then the result should be, in any order:
      | x    | ys     |
      | 1    | [1]    |
      | null | [null] |
      | 3    | [3]    |

  Scenario: pattern comprehension keeps a NULL projection
    Given an empty graph
    And having executed:
      """
      CREATE (:A {name: 'a'})-[:R]->(:B)
      """
    When executing query:
      """
      MATCH (a:A)
      RETURN [(a)-[:R]->(b) | b.name] AS names
      """
    Then the result should be, in any order:
      | names  |
      | [null] |
