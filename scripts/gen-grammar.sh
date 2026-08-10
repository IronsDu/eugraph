#!/bin/bash
#
# Regenerate ANTLR4 C++ sources for the Cypher parser.
#
# Cwd-independent: all paths resolved from the script's own location, so
# the script can be invoked from any directory. Idempotent: produces
# identical output on repeat runs, including the "Generated from
# <basename>.g4" comment (no absolute paths leak into the committed files).
#
# Why this exact shape:
# -cd into grammar/ and pass bare filenames so ANTLR's "Generated from"
#  comment uses just "CypherLexer.g4" / "CypherParser.g4" instead of an
#  absolute or repo-relative path.
# -Generate lexer first, then parser with -lib pointing at the output dir
#  so the parser can pick up CypherLexer.tokens. Without -lib, ANTLR
#  silently re-numbers tokens (EXPLAIN, COLONCOLON get appended at the
#  end as "implicit definitions") and shifts every token ID after them.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ANTLR_JAR="$PROJECT_ROOT/third_party/antlr-4.13.2-complete.jar"
GRAMMAR_DIR="$PROJECT_ROOT/grammar"
OUT_DIR="$PROJECT_ROOT/src/query/parser/generated/grammar"

if [[ ! -f "$ANTLR_JAR" ]]; then
    echo "Error: ANTLR jar not found at $ANTLR_JAR" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# Generate lexer first (produces CypherLexer.tokens needed by the parser).
( cd "$GRAMMAR_DIR" && \
  java -jar "$ANTLR_JAR" -Dlanguage=Cpp -visitor -no-listener -o "$OUT_DIR" CypherLexer.g4 )

# Generate parser with -lib so it sees CypherLexer.tokens.
( cd "$GRAMMAR_DIR" && \
  java -jar "$ANTLR_JAR" -Dlanguage=Cpp -visitor -no-listener -lib "$OUT_DIR" -o "$OUT_DIR" CypherParser.g4 )

echo "Generated Cypher parser sources in $OUT_DIR"
