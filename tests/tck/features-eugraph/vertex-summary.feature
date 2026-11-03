Feature: Vertex summary cache correctness

  # These scenarios exercise the read acceleration added for complex-3/9:
  # `vertex_summary` answers label/property probes from one point read and
  # must never return stale labels or values.  Every scenario ends with a
  # read that would expose a stale cache entry:
  #
  #   - CREATE writes the summary;
  #   - SET / REMOVE / SET label / REMOVE label / DELETE must invalidate it;
  #   - a property whose encoded value exceeds the 32-byte inline threshold
  #     is only listed as "present but omitted", so reads fall back.

  Scenario: Label scan reads properties written by CREATE
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person {name: 'Alice', age: 42})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS name, n.age AS age
      """
    Then the result should be, in any order:
      | name    | age |
      | 'Alice' | 42  |

  Scenario: Coalesce resolves a small property and treats a missing candidate as null
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person {name: 'Alice'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN coalesce(n.email, n.name) AS contact
      """
    Then the result should be, in any order:
      | contact |
      | 'Alice' |

  Scenario: A property above the inline threshold still reads correctly
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person {name: 'Alice', bio: '0123456789012345678901234567890123456789'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS name, n.bio AS bio
      """
    Then the result should be, in any order:
      | name    | bio                                        |
      | 'Alice' | '0123456789012345678901234567890123456789' |

  Scenario: SET replaces a value that was read through the summary
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person {name: 'Alice'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS first_read
      """
    Then the result should be, in any order:
      | first_read |
      | 'Alice'    |
    When executing query:
      """
      MATCH (n:Person)
      SET n.name = 'Bob'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS second_read
      """
    Then the result should be, in any order:
      | second_read |
      | 'Bob'       |

  Scenario: REMOVE makes a property read as null
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person {name: 'Alice', city: 'Paris'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.city AS city
      """
    Then the result should be, in any order:
      | city    |
      | 'Paris' |
    When executing query:
      """
      MATCH (n:Person)
      REMOVE n.city
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.city AS city
      """
    Then the result should be, in any order:
      | city |
      | null |

  Scenario: Adding a label invalidates cached labels
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person {name: 'Alice'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS name
      """
    Then the result should be, in any order:
      | name    |
      | 'Alice' |
    When executing query:
      """
      MATCH (n:Person)
      SET n:Employee
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Employee)
      RETURN n.name AS name
      """
    Then the result should be, in any order:
      | name    |
      | 'Alice' |

  Scenario: Removing a label invalidates cached labels
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person:Employee {name: 'Alice'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Employee)
      RETURN n.name AS name
      """
    Then the result should be, in any order:
      | name    |
      | 'Alice' |
    When executing query:
      """
      MATCH (n:Employee)
      REMOVE n:Employee
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Employee)
      RETURN n.name AS name
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS name
      """
    Then the result should be, in any order:
      | name    |
      | 'Alice' |

  Scenario: Deleting a vertex removes its summary
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person {name: 'Alice'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS name
      """
    Then the result should be, in any order:
      | name    |
      | 'Alice' |
    When executing query:
      """
      MATCH (n:Person)
      DETACH DELETE n
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS name
      """
    Then the result should be empty

  Scenario: MERGE create and match transitions keep property reads correct
    Given an empty graph
    When executing query:
      """
      MERGE (n:Person {name: 'Alice'})
      ON CREATE SET n.age = 1
      RETURN n.name AS name, n.age AS age
      """
    Then the result should be, in any order:
      | name    | age |
      | 'Alice' | 1   |
    When executing query:
      """
      MERGE (n:Person {name: 'Alice'})
      ON MATCH SET n.age = 2
      RETURN n.name AS name, n.age AS age
      """
    Then the result should be, in any order:
      | name    | age |
      | 'Alice' | 2   |

  Scenario: A vertex repeated across rows keeps per-row property alignment
    Given an empty graph
    When executing query:
      """
      CREATE (a:Person {name: 'A'}),
             (b:Person {name: 'B'}),
             (c:Person {name: 'C'}),
             (a)-[:KNOWS]->(b),
             (a)-[:KNOWS]->(c)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (a:Person)-[:KNOWS]->(b:Person)
      RETURN a.name AS a_name, b.name AS b_name
      ORDER BY b_name
      """
    Then the result should be, in order:
      | a_name | b_name |
      | 'A'    | 'B'    |
      | 'A'    | 'C'    |

  Scenario: A property set to null is not materialized in the summary
    Given an empty graph
    When executing query:
      """
      CREATE (n:Person {name: 'Alice'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      SET n.nickname = null
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.nickname AS nickname
      """
    Then the result should be, in any order:
      | nickname |
      | null     |

  Scenario: A list property above the inline threshold is read through the fallback
    Given an empty graph
    When executing query:
      """
      CREATE (n:Tagged {tags: ['alpha', 'bravo', 'charlie', 'delta', 'echo', 'foxtrot']})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:Tagged)
      RETURN size(n.tags) AS tag_count, n.tags[0] AS first_tag
      """
    Then the result should be, in any order:
      | tag_count | first_tag |
      | 6         | 'alpha'   |
