Feature: Path structure for self-loop relationships

  # A path is an alternating (node, relationship, node) sequence.  A self-loop
  # still contributes a closing node position: dropping it makes length(p) = 0,
  # size(nodes(p)) = 1, and corrupts the path sent over Bolt.

  Scenario: A directed self-loop path keeps its closing node
    Given an empty graph
    And having executed:
      """
      CREATE (n:A)-[:LOOP]->(n)
      """
    When executing query:
      """
      MATCH p = (:A)-->()
      RETURN length(p) AS len, size(nodes(p)) AS nodes, size(relationships(p)) AS rels
      """
    Then the result should be, in any order:
      | len | nodes | rels |
      | 1   | 2     | 1    |
    And no side effects

  Scenario: An incoming self-loop path keeps its closing node
    Given an empty graph
    And having executed:
      """
      CREATE (n:A)-[:LOOP]->(n)
      """
    When executing query:
      """
      MATCH p = (:A)<--()
      RETURN length(p) AS len, size(nodes(p)) AS nodes, size(relationships(p)) AS rels
      """
    Then the result should be, in any order:
      | len | nodes | rels |
      | 1   | 2     | 1    |
    And no side effects

  Scenario: An undirected self-loop path yields one path with two nodes
    Given an empty graph
    And having executed:
      """
      CREATE (n:A)-[:LOOP]->(n)
      """
    When executing query:
      """
      MATCH p = (:A)-[:LOOP]-(:A)
      RETURN length(p) AS len, size(nodes(p)) AS nodes, count(*) AS rows
      """
    Then the result should be, in any order:
      | len | nodes | rows |
      | 1   | 2     | 1    |
    And no side effects

  Scenario: Parallel self-loops each keep their closing node
    Given an empty graph
    And having executed:
      """
      CREATE (n:A)-[:LOOP]->(n), (n)-[:LOOP]->(n)
      """
    When executing query:
      """
      MATCH p = (:A)<--()
      RETURN length(p) AS len, size(nodes(p)) AS nodes
      """
    Then the result should be, in any order:
      | len | nodes |
      | 1   | 2     |
      | 1   | 2     |
    And no side effects

  Scenario: A multi-hop path keeps every node position
    Given an empty graph
    And having executed:
      """
      CREATE (a:A)-[:R]->(b:B)-[:R]->(c:C)
      """
    When executing query:
      """
      MATCH p = (:A)-[:R]->()-[:R]->()
      RETURN length(p) AS len, size(nodes(p)) AS nodes, size(relationships(p)) AS rels
      """
    Then the result should be, in any order:
      | len | nodes | rels |
      | 2   | 3     | 2    |
    And no side effects

  Scenario: A single-node path has one node and zero length
    Given an empty graph
    And having executed:
      """
      CREATE (n:A)
      """
    When executing query:
      """
      MATCH p = (n:A)
      RETURN length(p) AS len, size(nodes(p)) AS nodes, size(relationships(p)) AS rels
      """
    Then the result should be, in any order:
      | len | nodes | rels |
      | 0   | 1     | 0    |
    And no side effects
