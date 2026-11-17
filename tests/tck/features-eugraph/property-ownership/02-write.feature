# Property ownership - writes
#
# A weak write target is the property's single home; multiple homes make the
# write itself ambiguous. Strong writes name their home explicitly.
# Contract: docs/features/strong-weak-hybrid.md §5 / §6.

Feature: Property ownership - writes

  Background:
    Given an empty graph

  Scenario: Weak write on a vertex with two declaring labels is rejected
    Given the schema:
      """
      CREATE LABEL WriteTwoA (p STRING)
      CREATE LABEL WriteTwoB (p STRING)
      """
    When executing query:
      """
      CREATE (n:WriteTwoA:WriteTwoB)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteTwoA) SET n.p = 'x'
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:WriteTwoA) RETURN n::WriteTwoA.p AS p
      """
    Then the result should be, in any order:
      | p    |
      | null |

  Scenario: CREATE with a property declared by multiple labels is rejected
    Given the schema:
      """
      CREATE LABEL WriteCreateA (p STRING)
      CREATE LABEL WriteCreateB (p STRING)
      """
    When executing query:
      """
      CREATE (n:WriteCreateA:WriteCreateB {p: 'x'})
      """
    Then a SemanticError should be raised at any time: AmbiguousProperty

  Scenario: Strong write on an ambiguous vertex is allowed but does not resolve it
    Given the schema:
      """
      CREATE LABEL WriteAmb (p STRING)
      """
    When executing query:
      """
      CREATE (n {p: 'weak'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) SET n:WriteAmb
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteAmb) SET n::WriteAmb.p = 'strong'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteAmb) RETURN n.p
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:WriteAmb) RETURN n::WriteAmb.p AS p
      """
    Then the result should be, in any order:
      | p        |
      | 'strong' |

  Scenario: REMOVE p clears the ambiguity
    Given the schema:
      """
      CREATE LABEL WriteClear (p STRING)
      """
    When executing query:
      """
      CREATE (n {p: 'weak'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) SET n:WriteClear
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteClear) REMOVE n.p
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteClear) SET n::WriteClear.p = 'strong'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteClear) RETURN n.p AS p
      """
    Then the result should be, in any order:
      | p        |
      | 'strong' |

  Scenario: Adding a declaring label is allowed when no index forbids it
    Given the schema:
      """
      CREATE LABEL WriteAdd (p STRING)
      """
    When executing query:
      """
      CREATE (n {p: 'weak'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) SET n:WriteAdd
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteAdd) RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 1 |

  Scenario: SET n.p = null deletes every home
    Given the schema:
      """
      CREATE LABEL WriteNull (p STRING)
      """
    When executing query:
      """
      CREATE (n {p: 'weak'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) SET n:WriteNull
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteNull) SET n.p = null
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteNull) RETURN n.p AS p
      """
    Then the result should be, in any order:
      | p    |
      | null |
    When executing query:
      """
      MATCH (n:WriteNull) SET n::WriteNull.p = 'strong'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteNull) RETURN n.p AS p
      """
    Then the result should be, in any order:
      | p        |
      | 'strong' |

  Scenario: SET map operators reject an ambiguous key
    Given the schema:
      """
      CREATE LABEL WriteMapA (p STRING)
      CREATE LABEL WriteMapB (p STRING)
      """
    When executing query:
      """
      CREATE (n:WriteMapA:WriteMapB)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteMapA) SET n = {p: 'x'}
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:WriteMapA) SET n += {p: 'x'}
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty

  Scenario: Strong write to an undeclared property is rejected
    Given the schema:
      """
      CREATE LABEL WriteStrongErr (id:INT64)
      """
    When executing query:
      """
      CREATE (n:WriteStrongErr {id: 1})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteStrongErr) SET n::WriteStrongErr.missing = 1
      """
    Then a RuntimeError should be raised at any time: *

  Scenario: REMOVE n:Label leaves a bare vertex and drops its properties
    Given the schema:
      """
      CREATE LABEL WriteBare (p STRING)
      """
    When executing query:
      """
      CREATE (n:WriteBare {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:WriteBare) REMOVE n:WriteBare
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) RETURN labels(n) AS ls, n.p AS p, keys(n) AS ks
      """
    Then the result should be, in any order:
      | ls | p    | ks |
      | [] | null | [] |
