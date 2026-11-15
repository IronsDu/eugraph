# Property ownership - indexes
#
# A weak index materializes the weak view, so it can only exist when no
# ambiguous vertex does; it also rejects writes that would create ambiguity.
# A strong index only materializes the source label's column and therefore
# does not block label additions that make the weak view ambiguous.
# Contract: docs/features/strong-weak-hybrid.md §7.

Feature: Property ownership - indexes

  Background:
    Given an empty graph

  Scenario: A weak index cannot be built over an ambiguous vertex
    Given the schema:
      """
      CREATE LABEL IdxAmbA (p STRING)
      CREATE LABEL IdxAmbB (p STRING)
      """
    When executing query:
      """
      CREATE (n:IdxAmbA:IdxAmbB)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:IdxAmbA) SET n::IdxAmbA.p = 'a', n::IdxAmbB.p = 'b'
      """
    Then the result should be empty
    When executing query:
      """
      CREATE INDEX idx_po_weak_ambiguous FOR (n:IdxAmbA) ON (n.p)
      """
    Then a RuntimeError should be raised at any time: *

  Scenario: A weak index rejects the write that would create ambiguity
    Given the schema:
      """
      CREATE LABEL IdxGuardA (p STRING)
      CREATE LABEL IdxGuardB (p STRING)
      """
    When executing query:
      """
      CREATE (n:IdxGuardA {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      CREATE INDEX idx_po_weak_guard FOR (n:IdxGuardA) ON (n.p)
      """
    Then the result should be, in any order:
      | result                              |
      | 'Index created: idx_po_weak_guard'  |
    When executing query:
      """
      MATCH (n:IdxGuardA) SET n:IdxGuardB
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:IdxGuardB) RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 0 |

  Scenario: A strong index allows a declaring-label addition
    Given the schema:
      """
      CREATE LABEL IdxStrongA (p STRING)
      CREATE LABEL IdxStrongB (p STRING)
      """
    When executing query:
      """
      CREATE (n:IdxStrongA {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      CREATE INDEX idx_po_strong_ok FOR (n:IdxStrongA) ON (n::IdxStrongA.p)
      """
    Then the result should be, in any order:
      | result                                 |
      | 'Index created: idx_po_strong_ok'      |
    When executing query:
      """
      MATCH (n:IdxStrongA) SET n:IdxStrongB
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) RETURN n.p
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty

  Scenario: A weak index serves a weak query
    Given the schema:
      """
      CREATE LABEL IdxServe (p STRING)
      """
    When executing query:
      """
      CREATE (n:IdxServe {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      CREATE INDEX idx_po_weak_serve FOR (n:IdxServe) ON (n.p)
      """
    Then the result should be, in any order:
      | result                              |
      | 'Index created: idx_po_weak_serve'  |
    When executing query:
      """
      MATCH (n:IdxServe) WHERE n.p = 'v' RETURN n.p AS p
      """
    Then the result should be, in any order:
      | p   |
      | 'v' |

  Scenario: A strong index ignores anon values
    Given the schema:
      """
      CREATE LABEL IdxAnon (p STRING)
      """
    When executing query:
      """
      CREATE (n {p: 'weak'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) SET n:IdxAnon
      """
    Then the result should be empty
    When executing query:
      """
      CREATE INDEX idx_po_strong_anon FOR (n:IdxAnon) ON (n::IdxAnon.p)
      """
    Then the result should be, in any order:
      | result                                  |
      | 'Index created: idx_po_strong_anon'     |
    When executing query:
      """
      MATCH (n:IdxAnon) RETURN n::IdxAnon.p AS p
      """
    Then the result should be, in any order:
      | p    |
      | null |

  Scenario: A strong index requires the source label to declare the property
    Given the schema:
      """
      CREATE LABEL IdxDdl (id:INT64)
      """
    When executing query:
      """
      CREATE INDEX idx_po_strong_undeclared FOR (n:IdxDdl) ON (n::IdxDdl.missing)
      """
    Then a RuntimeError should be raised at any time: *

  Scenario: A weak index may be created before the key exists
    Given the schema:
      """
      CREATE LABEL IdxLazy (id:INT64)
      """
    When executing query:
      """
      CREATE INDEX idx_po_lazy FOR (n:IdxLazy) ON (n.nickname)
      """
    Then the result should be, in any order:
      | result                           |
      | 'Index created: idx_po_lazy'     |
    When executing query:
      """
      CREATE (n:IdxLazy {id: 1, nickname: 'nick'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:IdxLazy) WHERE n.nickname = 'nick' RETURN n.nickname AS v
      """
    Then the result should be, in any order:
      | v      |
      | 'nick' |

  Scenario: A strong index serves a weak query when no ambiguity is possible
    Given the schema:
      """
      CREATE LABEL IdxStrongWeak (p:STRING)
      """
    When executing query:
      """
      CREATE (n:IdxStrongWeak {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      CREATE INDEX idx_po_strong_weak FOR (n:IdxStrongWeak) ON (n::IdxStrongWeak.p)
      """
    Then the result should be, in any order:
      | result                                    |
      | 'Index created: idx_po_strong_weak'       |
    When executing query:
      """
      MATCH (n:IdxStrongWeak) WHERE n.p = 'v' RETURN n.p AS p
      """
    Then the result should be, in any order:
      | p   |
      | 'v' |

  Scenario: A weak index blocks MERGE SET n:Label that creates ambiguity
    Given the schema:
      """
      CREATE LABEL IdxMergeA (p:STRING)
      CREATE LABEL IdxMergeB (p:STRING)
      """
    When executing query:
      """
      CREATE (n:IdxMergeA {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      CREATE INDEX idx_po_merge_guard FOR (n:IdxMergeA) ON (n.p)
      """
    Then the result should be, in any order:
      | result                                      |
      | 'Index created: idx_po_merge_guard'         |
    When executing query:
      """
      MERGE (n:IdxMergeA {p: 'v'}) ON MATCH SET n:IdxMergeB
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:IdxMergeB) RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 0 |
