export type ParsedInput =
  | { kind: 'meta'; command: string; args: string }
  | { kind: 'query'; statements: string[] };

const META_RE = /^:([A-Za-z][A-Za-z0-9_-]*)(?:\s+([\s\S]*))?$/;

export function parseInput(input: string): ParsedInput {
  const trimmed = input.trim();
  const meta = META_RE.exec(trimmed);
  if (meta) {
    return { kind: 'meta', command: meta[1].toLowerCase(), args: (meta[2] ?? '').trim() };
  }
  return { kind: 'query', statements: splitStatements(trimmed) };
}

/**
 * Split a Cypher script at top-level semicolons. Semicolons inside strings,
 * backtick identifiers, line comments and block comments are preserved.
 */
export function splitStatements(input: string): string[] {
  const statements: string[] = [];
  let current = '';
  let quote: "'" | '"' | '`' | null = null;
  let escaped = false;
  let lineComment = false;
  let blockComment = false;

  for (let index = 0; index < input.length; index += 1) {
    const char = input[index];
    const next = input[index + 1];

    if (lineComment) {
      current += char;
      if (char === '\n') lineComment = false;
      continue;
    }
    if (blockComment) {
      current += char;
      if (char === '*' && next === '/') {
        current += next;
        index += 1;
        blockComment = false;
      }
      continue;
    }
    if (quote !== null) {
      current += char;
      if (escaped) {
        escaped = false;
      } else if (char === '\\' && quote !== '`') {
        escaped = true;
      } else if (char === quote) {
        quote = null;
      }
      continue;
    }

    if (char === '/' && next === '/') {
      lineComment = true;
      current += char;
      continue;
    }
    if (char === '/' && next === '*') {
      blockComment = true;
      current += char;
      continue;
    }
    if (char === "'" || char === '"' || char === '`') {
      quote = char;
      current += char;
      continue;
    }
    if (char === ';') {
      const statement = current.trim();
      if (statement) statements.push(statement);
      current = '';
      continue;
    }
    current += char;
  }

  const tail = current.trim();
  if (tail) statements.push(tail);
  return statements;
}
