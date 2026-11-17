# Property ownership - introspection
#
# properties(n) returns a Cypher MAP. A map cannot hold two entries for the
# same key, so any key with 2+ homes (even with equal values, or a NULL column
# plus an anon value) is ambiguous -> error. keys(n) returns the de-duplicated
# list of property NAMES; a name set is unambiguous and stays usable.
# Contract: docs/features/strong-weak-hybrid.md §4.

Feature: Property ownership - introspection

  Background:
    Given an empty graph

  Scenario: properties returns the value and keys the name for a single home
    Given the schema:
      """
      CREATE LABEL IntroOne (p STRING)
      """
    When executing query:
      """
      CREATE (n:IntroOne {p: 'v'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:IntroOne) RETURN properties(n).p AS v, keys(n) AS ks
      """
    Then the result should be, in any order:
      | v   | ks    |
      | 'v' | ['p'] |

  Scenario: properties rejects two declaring labels even with equal values
    Given the schema:
      """
      CREATE LABEL IntroEqA (p STRING)
      CREATE LABEL IntroEqB (p STRING)
      """
    When executing query:
      """
      CREATE (n:IntroEqA:IntroEqB)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:IntroEqA) SET n::IntroEqA.p = 'v', n::IntroEqB.p = 'v'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:IntroEqA) RETURN properties(n)
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:IntroEqA) RETURN keys(n) AS ks
      """
    Then the result should be, in any order:
      | ks    |
      | ['p'] |

  Scenario: properties rejects an anon value plus a declaring label with NULL
    Given the schema:
      """
      CREATE LABEL IntroAnon (p STRING)
      """
    When executing query:
      """
      CREATE (n {p: 'weak'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) SET n:IntroAnon
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:IntroAnon) RETURN properties(n)
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:IntroAnon) RETURN keys(n) AS ks
      """
    Then the result should be, in any order:
      | ks    |
      | ['p'] |

  Scenario: properties rejects two declaring labels with different values
    Given the schema:
      """
      CREATE LABEL IntroDiffA (p STRING)
      CREATE LABEL IntroDiffB (p STRING)
      """
    When executing query:
      """
      CREATE (n:IntroDiffA:IntroDiffB)
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:IntroDiffA) SET n::IntroDiffA.p = 'a', n::IntroDiffB.p = 'b'
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:IntroDiffA) RETURN properties(n)
      """
    Then a SemanticError should be raised at runtime: AmbiguousProperty
    When executing query:
      """
      MATCH (n:IntroDiffA) RETURN keys(n) AS ks
      """
    Then the result should be, in any order:
      | ks    |
      | ['p'] |

  Scenario: properties works for an anon-only vertex
    When executing query:
      """
      CREATE ({q: 'x'})
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n) RETURN properties(n).q AS v, keys(n) AS ks
      """
    Then the result should be, in any order:
      | v   | ks    |
      | 'x' | ['q'] |
