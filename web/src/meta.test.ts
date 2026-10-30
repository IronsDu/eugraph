import { describe, expect, it } from 'vitest';
import { parseInput, splitStatements } from './meta';

describe('splitStatements', () => {
  it('splits on top-level semicolons', () => {
    expect(splitStatements('RETURN 1; RETURN 2;')).toEqual(['RETURN 1', 'RETURN 2']);
  });

  it('ignores semicolons in strings, backticks and comments', () => {
    const input =
      "RETURN 'a;b' AS x;// c;\nRETURN \"c;d\" AS y;/* block ; comment */ MATCH (`weird;label`) RETURN 1;";
    expect(splitStatements(input)).toEqual([
      "RETURN 'a;b' AS x",
      '// c;\nRETURN "c;d" AS y',
      '/* block ; comment */ MATCH (`weird;label`) RETURN 1',
    ]);
  });

  it('keeps escaped quotes', () => {
    expect(splitStatements("RETURN 'it\\'s;ok' AS v; RETURN 2")).toEqual(["RETURN 'it\\'s;ok' AS v", 'RETURN 2']);
  });

  it('drops empty statements', () => {
    expect(splitStatements(' ; ; RETURN 1 ;; ')).toEqual(['RETURN 1']);
  });
});

describe('parseInput', () => {
  it('parses meta commands', () => {
    expect(parseInput(':use other')).toEqual({ kind: 'meta', command: 'use', args: 'other' });
    expect(parseInput(':PARAMS clear')).toEqual({ kind: 'meta', command: 'params', args: 'clear' });
  });

  it('parses multi-statement queries', () => {
    expect(parseInput('RETURN 1; RETURN 2')).toEqual({ kind: 'query', statements: ['RETURN 1', 'RETURN 2'] });
  });
});
