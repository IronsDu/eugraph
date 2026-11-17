# Property ownership - reads
#
# A property has HOMES on a vertex:
#   - every held label that declares it (the column exists even if NULL), and
#   - __anon__ when the vertex holds a value there.
# More than one home is ambiguity: the weak view has no single value.
# Contract: docs/features/strong-weak-hybrid.md §4.

Feature: Property ownership - reads

  Background:
    Given an empty graph

  Scenario: A single declared home reads its value
    Given the schema:
      """
      CREATE LABEL ReadOne (p STRING)
      """
    When executing query:
      """
      CREATE (n:ReadOne {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadOne) RETURN n.p AS weak, n::ReadOne.p AS strong
      """
    Then the result should be, in any order:
      | weak | strong |
      | 'v'  | 'v'    |

  Scenario: An unlabeled vertex reads its weak value
    When executing query:
      """
      CREATE (n {q: 'anon'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) RETURN n.q AS q
      """
    Then the result should be, in any order:
      | q      |
      | 'anon' |

  Scenario: Two declaring labels are ambiguous even when one side is NULL
    Given the schema:
      """
      CREATE LABEL ReadTwoA (p STRING)
      CREATE LABEL ReadTwoB (p STRING)
      """
    When executing query:
      """
      CREATE (n:ReadTwoA {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadTwoA) SET n:ReadTwoB
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadTwoA) RETURN n.p
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:ReadTwoA) RETURN n::ReadTwoA.p AS a, n::ReadTwoB.p AS b
      """
    Then the result should be, in any order:
      | a   | b    |
      | 'v' | null |

  Scenario: Equal values under two declaring labels are still ambiguous
    Given the schema:
      """
      CREATE LABEL ReadEqA (p STRING)
      CREATE LABEL ReadEqB (p STRING)
      """
    When executing query:
      """
      CREATE (n:ReadEqA:ReadEqB)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadEqA) SET n::ReadEqA.p = 'same', n::ReadEqB.p = 'same'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadEqA) RETURN n.p
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty

  Scenario: An anon value plus a declaring label is ambiguous
    Given the schema:
      """
      CREATE LABEL ReadAnon (p STRING)
      """
    When executing query:
      """
      CREATE (n {p: 'weak'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) SET n:ReadAnon
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadAnon) RETURN n.p
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:ReadAnon) RETURN n::ReadAnon.p AS p
      """
    Then the result should be, in any order:
      | p    |
      | null |

  Scenario: Dynamic access n[propName] follows the home rule
    Given the schema:
      """
      CREATE LABEL DynRead (p STRING)
      """
    When executing query:
      """
      CREATE (n:DynRead {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:DynRead) RETURN n['p'] AS v
      """
    Then the result should be, in any order:
      | v   |
      | 'v' |
    Given the schema:
      """
      CREATE LABEL DynReadA (p STRING)
      CREATE LABEL DynReadB (p STRING)
      """
    When executing query:
      """
      CREATE (n:DynReadA:DynReadB)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:DynReadA) SET n::DynReadA.p = 'a', n::DynReadB.p = 'b'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:DynReadA) RETURN n['p']
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty

  Scenario: ORDER BY and aggregation reject an ambiguous property
    Given the schema:
      """
      CREATE LABEL ReadSortA (p:STRING)
      CREATE LABEL ReadSortB (p:STRING)
      """
    When executing query:
      """
      CREATE (n:ReadSortA:ReadSortB)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadSortA) SET n::ReadSortA.p = 'a', n::ReadSortB.p = 'b'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadSortA) RETURN n.p ORDER BY n.p
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:ReadSortA) RETURN count(n.p) AS c
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty

  Scenario: A strong cast scopes a vertex to the cast label
    Given the schema:
      """
      CREATE LABEL ReadCast (p:STRING)
      """
    When executing query:
      """
      CREATE (n:ReadCast {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:ReadCast) RETURN keys(n::ReadCast) AS ks
      """
    Then the result should be, in any order:
      | ks    |
      | ['p'] |
    When executing query:
      """
      CREATE (m {x: 1})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (m {x: 1}) RETURN size(keys(m::ReadCast)) AS fields
      """
    Then the result should be, in any order:
      | fields |
      | 0      |
