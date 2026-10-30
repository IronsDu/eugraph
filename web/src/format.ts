const MAX_INLINE_PROPERTIES = 8;
const MAX_PROPERTY_LENGTH = 96;
const MAX_VALUE_LENGTH = 720;
const MAX_CONTAINER_ITEMS = 8;

export function isNodeValue(value: any): boolean {
  return Array.isArray(value?.labels) && value?.properties !== undefined && value?.elementId !== undefined;
}

export function isRelationshipValue(value: any): boolean {
  return (
    typeof value?.type === 'string' &&
    value?.properties !== undefined &&
    value?.elementId !== undefined &&
    (value?.startNodeElementId !== undefined || value?.start !== undefined)
  );
}

export function isPathValue(value: any): boolean {
  return Array.isArray(value?.segments) || (Array.isArray(value?.nodes) && Array.isArray(value?.relationships));
}

function truncate(value: string, max: number): string {
  return value.length <= max ? value : `${value.slice(0, max - 1)}…`;
}

function formatPropertyValue(value: unknown, depth: number): string {
  if (value === null || value === undefined) return 'null';
  if (typeof value === 'string') return JSON.stringify(value);
  if (typeof value === 'number' || typeof value === 'boolean' || typeof value === 'bigint') {
    return String(value);
  }
  if (Array.isArray(value)) {
    const shown = value.slice(0, MAX_CONTAINER_ITEMS).map((item) => formatPropertyValue(item, depth + 1));
    if (value.length > shown.length) shown.push('…');
    return `[${shown.join(', ')}]`;
  }

  const candidate = value as Record<string, any>;
  if (candidate.year !== undefined || candidate.months !== undefined || candidate.nanoseconds !== undefined) {
    try {
      return String(value);
    } catch {
      /* fall through */
    }
  }
  if (depth < 3) {
    try {
      const json = JSON.stringify(value);
      if (json !== undefined) return json;
    } catch {
      /* fall through */
    }
  }
  return String(value);
}

export function orderedPropertyEntries(properties: Record<string, any> | undefined): Array<[string, any]> {
  const priority = (key: string): number => (key === 'name' ? 0 : key === 'title' ? 1 : key === 'id' ? 2 : 3);
  return Object.entries(properties ?? {}).sort(([a], [b]) => {
    const byPriority = priority(a) - priority(b);
    return byPriority !== 0 ? byPriority : a.localeCompare(b);
  });
}

function formatProperties(properties: Record<string, any> | undefined, depth: number): string {
  const entries = orderedPropertyEntries(properties);
  if (entries.length === 0) return '';
  const shown = entries.slice(0, MAX_INLINE_PROPERTIES).map(([key, value]) => {
    return `${key}: ${truncate(formatPropertyValue(value, depth + 1), MAX_PROPERTY_LENGTH)}`;
  });
  if (entries.length > MAX_INLINE_PROPERTIES) shown.push('…');
  return ` {${shown.join(', ')}}`;
}

function nodeText(value: any): string {
  const labels = Array.isArray(value.labels) ? (value.labels as string[]) : [];
  const labelText = labels.length > 0 ? `:${labels.join(':')}` : '';
  return `(${labelText}${formatProperties(value.properties, 0)})`;
}

function relationshipText(value: any): string {
  return `[:${value.type}${formatProperties(value.properties, 0)}]`;
}

function pathText(value: any): string {
  const segments = Array.isArray(value.segments) ? value.segments : [];
  if (segments.length === 0) {
    const nodes = Array.isArray(value.nodes) ? value.nodes : [];
    if (nodes.length === 0) return '()';
    return nodes.map((node: any) => nodeText(node)).join(' -> ');
  }
  let text = nodeText(segments[0].start);
  for (const segment of segments) {
    text += `-[:${segment.relationship?.type ?? ''}]->${nodeText(segment.end)}`;
  }
  return text;
}

function formatInternal(value: unknown, depth: number): string {
  if (value === null || value === undefined) return 'null';
  if (typeof value === 'string') return value;
  if (typeof value === 'number' || typeof value === 'boolean' || typeof value === 'bigint') {
    return String(value);
  }

  const candidate = value as Record<string, any>;
  if (isNodeValue(candidate)) return nodeText(candidate);
  if (isRelationshipValue(candidate)) return relationshipText(candidate);
  if (isPathValue(candidate)) return pathText(candidate);
  if (Array.isArray(value)) {
    return `[${value.map((item) => formatInternal(item, depth + 1)).join(', ')}]`;
  }

  if (candidate.year !== undefined || candidate.months !== undefined || candidate.nanoseconds !== undefined) {
    try {
      return String(value);
    } catch {
      /* fall through */
    }
  }

  try {
    const json = JSON.stringify(value);
    return json === undefined ? String(value) : json;
  } catch {
    return String(value);
  }
}

/** Human-readable, Neo4j-Browser-like rendering for table cells and CSV. */
export function formatValue(value: unknown): string {
  return truncate(formatInternal(value, 0), MAX_VALUE_LENGTH);
}

/** Plain JSON-able representation used by the value inspector. */
export function toPlainValue(value: any, depth = 0): unknown {
  if (value === null || value === undefined) return null;
  if (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') return value;
  if (typeof value === 'bigint') return value.toString();
  if (depth > 6) return String(value);

  if (isNodeValue(value)) {
    return {
      kind: 'node',
      elementId: value.elementId,
      labels: value.labels,
      properties: toPlainValue(value.properties ?? {}, depth + 1),
    };
  }
  if (isRelationshipValue(value)) {
    return {
      kind: 'relationship',
      elementId: value.elementId,
      type: value.type,
      startNodeElementId: value.startNodeElementId ?? value.start,
      endNodeElementId: value.endNodeElementId ?? value.end,
      properties: toPlainValue(value.properties ?? {}, depth + 1),
    };
  }
  if (isPathValue(value)) {
    return {
      kind: 'path',
      segments: (value.segments ?? []).map((segment: any) => ({
        start: toPlainValue(segment.start, depth + 1),
        relationship: toPlainValue(segment.relationship, depth + 1),
        end: toPlainValue(segment.end, depth + 1),
      })),
    };
  }
  if (Array.isArray(value)) return value.map((item) => toPlainValue(item, depth + 1));
  if (value?.year !== undefined || value?.months !== undefined || value?.nanoseconds !== undefined) {
    try {
      return String(value);
    } catch {
      /* fall through */
    }
  }
  if (typeof value === 'object') {
    const result: Record<string, unknown> = {};
    for (const [key, item] of Object.entries(value)) result[key] = toPlainValue(item, depth + 1);
    return result;
  }
  return String(value);
}

export function csvCell(value: unknown): string {
  const text = formatValue(value);
  if (/[",\r\n]/.test(text)) return `"${text.replace(/"/g, '""')}"`;
  return text;
}

export function recordsToCsv(columns: string[], records: any[]): string {
  const lines = [columns.map(csvCell).join(',')];
  for (const record of records) {
    lines.push(columns.map((column) => csvCell(record.get(column))).join(','));
  }
  return lines.join('\n') + '\n';
}
