import { describe, expect, it } from 'vitest';
import { truncateCaptionToWidth } from './caption';

const measure = (text: string): number => text.length * 10;

describe('truncateCaptionToWidth', () => {
  it('keeps text that already fits', () => {
    expect(truncateCaptionToWidth('Alice', 50, measure)).toBe('Alice');
  });

  it('truncates with an ellipsis and respects the width', () => {
    const result = truncateCaptionToWidth('abcdefghij', 50, measure);
    expect(result).toBe('abcd…');
    expect(measure(result)).toBeLessThanOrEqual(50);
  });

  it('handles unicode characters without splitting them', () => {
    const result = truncateCaptionToWidth('上海浦东新区', 40, measure);
    expect(result).toBe('上海浦…');
    expect(Array.from(result).every((character) => character.length === 1)).toBe(true);
  });

  it('returns an empty string when even the ellipsis cannot fit', () => {
    expect(truncateCaptionToWidth('Alice', 5, measure)).toBe('');
  });
});
