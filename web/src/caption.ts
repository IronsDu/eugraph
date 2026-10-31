/**
 * Truncate text so its measured width does not exceed maxWidth.
 * The ellipsis is included in the measurement. Returns an empty string when
 * even the ellipsis cannot fit.
 */
export function truncateCaptionToWidth(
  text: string,
  maxWidth: number,
  measure: (text: string) => number
): string {
  const caption = text.trim();
  if (!caption || maxWidth <= 0) return '';
  if (measure(caption) <= maxWidth) return caption;

  const ellipsis = '…';
  if (measure(ellipsis) > maxWidth) return '';

  const characters = Array.from(caption);
  let low = 0;
  let high = characters.length;
  while (low < high) {
    const middle = Math.ceil((low + high) / 2);
    const candidate = `${characters.slice(0, middle).join('')}${ellipsis}`;
    if (measure(candidate) <= maxWidth) low = middle;
    else high = middle - 1;
  }
  return `${characters.slice(0, low).join('')}${ellipsis}`;
}
