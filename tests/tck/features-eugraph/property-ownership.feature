# Property ownership: strong vs weak space
#
# Contract: docs/features/property-ownership.md
#   - DML never declares label columns; undeclared keys live in __anon__.
#   - Weak access (n.p) searches every source and requires a single value.
#   - Strong access (n::L.p) only reads/writes L's declared column.
#   - REMOVE n:L deletes that label's property rows.

Feature: Property ownership

  Background:
    Given an empty graph

  Scenario: CREATE auto-creates an empty label and keeps undeclared properties in the weak space
    When executing query:
      """
      CREATE (:WeakTag {a: 1, b: 'x'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WeakTag) RETURN n.a AS a, n.b AS b
      """
    Then the result should be, in any order:
      | a | b   |
      | 1 | 'x' |
    When executing query:
      """
      DESCRIBE LABEL WeakTag
      """
    Then the result should be empty

  Scenario: Declared properties stay in the label schema
    Given a label "StrongTag" with properties "p:INT64"
    When executing query:
      """
      CREATE (n:StrongTag {p: 1})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:StrongTag) RETURN n::StrongTag.p AS p
      """
    Then the result should be, in any order:
      | p |
      | 1 |
    When executing query:
      """
      DESCRIBE LABEL StrongTag
      """
    Then the result should be, in any order:
      | label       | propertyName | propertyType |
      | 'StrongTag' | 'p'          | 'INTEGER'    |

  Scenario: A property declared by two labels of the same CREATE is ambiguous
    Given a label "DualA" with properties "p:INT64"
    Given a label "DualB" with properties "p:INT64"
    When executing query:
      """
      CREATE (n:DualA:DualB {p: 1})
      """
    Then a SemanticError should be raised at any time: AmbiguousProperty

  Scenario: Conflicting weak reads report an error; strong reads select one scope
    Given a label "ConfA" with properties "p:STRING"
    Given a label "ConfB" with properties "p:STRING"
    Given having executed:
      """
      CREATE (n:ConfA:ConfB)
      """
    Given having executed:
      """
      MATCH (n:ConfA) SET n::ConfA.p = 'a'
      """
    Given having executed:
      """
      MATCH (n:ConfB) SET n::ConfB.p = 'b'
      """
    When executing query:
      """
      MATCH (n:ConfA) RETURN n.p
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:ConfA) RETURN n::ConfA.p AS a, n::ConfB.p AS b
      """
    Then the result should be, in any order:
      | a   | b   |
      | 'a' | 'b' |

  Scenario: Equal values under several labels are not a conflict
    Given a label "SameA" with properties "p:STRING"
    Given a label "SameB" with properties "p:STRING"
    Given having executed:
      """
      CREATE (n:SameA:SameB)
      """
    Given having executed:
      """
      MATCH (n:SameA) SET n::SameA.p = 'same', n::SameB.p = 'same'
      """
    When executing query:
      """
      MATCH (n:SameA) RETURN n.p AS p
      """
    Then the result should be, in any order:
      | p      |
      | 'same' |

  Scenario: REMOVE label deletes only that label's properties
    Given a label "RemTag" with properties "p:STRING"
    Given having executed:
      """
      CREATE (n:RemTag {p: 'v', w: 'weak'})
      """
    Given having executed:
      """
      MATCH (n:RemTag) REMOVE n:RemTag
      """
    When executing query:
      """
      MATCH (n) RETURN n.p AS p, n.w AS w
      """
    Then the result should be, in any order:
      | p    | w      |
      | null | 'weak' |
    When executing query:
      """
      MATCH (n:RemTag) RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 0 |

  Scenario: MERGE keeps undeclared properties in the weak space and matches them
    Given a label "MergeTag" with properties "id:INT64"
    Given having executed:
      """
      MERGE (n:MergeTag {id: 1, tag: 'x'})
      """
    When executing query:
      """
      MERGE (n:MergeTag {id: 1, tag: 'x'}) RETURN n.tag AS t
      """
    Then the result should be, in any order:
      | t   |
      | 'x' |
    When executing query:
      """
      MATCH (n:MergeTag) RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 1 |
    When executing query:
      """
      DESCRIBE LABEL MergeTag
      """
    Then the result should be, in any order:
      | label      | propertyName | propertyType |
      | 'MergeTag' | 'id'         | 'INTEGER'    |
