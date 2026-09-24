Feature: FOREACH clause

  Background:
    Given an empty graph

  # Expected values come from neo4j 5 measured on the same graph shape; see the
  # FOREACH section of docs/query/syntax/cypher-syntax.md.
  #
  # Note the space in `{v: x}`: the TCK helper scans a query for ":Name" to create
  # labels up front, and `{v:x}` would register a label named `x`.

  Scenario: FOREACH creates one node per element
    When executing query:
      """
      FOREACH (x IN [1, 2, 3] | CREATE (:FT_T {v: x}))
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (t:FT_T) RETURN count(t) AS c
      """
    Then the result should be, in any order:
      | c |
      | 3 |

  Scenario: An empty list runs the body zero times and is not an error
    When executing query:
      """
      FOREACH (x IN [] | CREATE (:FT_T {v: x}))
      """
    Then no side effects

  Scenario: A null list runs the body zero times and is not an error
    When executing query:
      """
      FOREACH (x IN null | CREATE (:FT_T {v: x}))
      """
    Then no side effects

  # neo4j treats a non-list value as a one-element list rather than failing.
  Scenario: A non-list value iterates exactly once
    When executing query:
      """
      FOREACH (x IN 1 | CREATE (:FT_T {v: x}))
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (t:FT_T) RETURN t.v AS v
      """
    Then the result should be, in any order:
      | v |
      | 1 |

  # Unlike UNWIND, FOREACH hands its input rows through: one row out per row in,
  # however many elements the body processed.
  Scenario: FOREACH keeps the input cardinality
    When executing query:
      """
      CREATE (:FT_P {id: 1})
      """
    Then the result should be empty
    When executing query:
      """
      CREATE (:FT_P {id: 2})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (p:FT_P) FOREACH (x IN [1, 2] | CREATE (:FT_T {v: x})) RETURN count(p) AS c
      """
    Then the result should be, in any order:
      | c |
      | 2 |
    When executing query:
      """
      MATCH (t:FT_T) RETURN count(t) AS c
      """
    Then the result should be, in any order:
      | c |
      | 4 |

  Scenario: The body can read variables from the surrounding scope
    When executing query:
      """
      CREATE (:FT_P {id: 7})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (p:FT_P) FOREACH (x IN [1, 2] | CREATE (:FT_T {v: p.id + x}))
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (t:FT_T) RETURN t.v AS v ORDER BY v
      """
    Then the result should be, in order:
      | v |
      | 8 |
      | 9 |

  Scenario: Writes to an outer variable are visible to later clauses
    When executing query:
      """
      CREATE (:FT_P {id: 1})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (p:FT_P) FOREACH (x IN [1, 2, 3] | SET p.total = coalesce(p.total, 0) + x) RETURN p.total AS t
      """
    Then the result should be, in any order:
      | t |
      | 6 |
    When executing query:
      """
      MATCH (p:FT_P) FOREACH (x IN [1] | SET p.hit = true) WITH p RETURN p.hit AS hit
      """
    Then the result should be, in any order:
      | hit  |
      | true |

  Scenario: Nested FOREACH runs the inner body for every outer element
    When executing query:
      """
      FOREACH (x IN [1, 2] | FOREACH (y IN [10, 20] | CREATE (:FT_T {a: x, b: y})))
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (t:FT_T) RETURN count(t) AS c
      """
    Then the result should be, in any order:
      | c |
      | 4 |

  # The element shadows an outer variable of the same name inside the body only.
  Scenario: The element variable shadows an outer variable
    When executing query:
      """
      CREATE (:FT_P {id: 1})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:FT_P) FOREACH (n IN [1] | CREATE (:FT_T {v: n})) RETURN count(n) AS c
      """
    Then the result should be, in any order:
      | c |
      | 1 |
    When executing query:
      """
      MATCH (t:FT_T) RETURN t.v AS v
      """
    Then the result should be, in any order:
      | v |
      | 1 |

  Scenario: DELETE in the body removes the outer row's node
    When executing query:
      """
      CREATE (:FT_P {id: 1})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (p:FT_P) FOREACH (x IN [1] | DETACH DELETE p)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (p:FT_P) RETURN count(p) AS c
      """
    Then the result should be, in any order:
      | c |
      | 0 |

  # The variable context of a FOREACH is separate from the outside one (neo4j:
  # "you will not be able to use it outside of the foreach statement").
  Scenario: The element variable is not visible after the clause
    When executing query:
      """
      FOREACH (x IN [1] | CREATE (:FT_T {v: x})) RETURN x
      """
    Then a SyntaxError should be raised at any time: UndefinedVariable

  # The body accepts updating clauses only; the grammar rejects a reading clause.
  Scenario: A reading clause inside the body is a syntax error
    When executing query:
      """
      FOREACH (x IN [1] | MATCH (n) RETURN n)
      """
    Then a SyntaxError should be raised at any time: *
