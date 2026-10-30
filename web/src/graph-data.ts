import { MultiDirectedGraph } from 'graphology';

export const GRAPH_LIMITS = { nodes: 2000, edges: 5000 };

export type GraphResult = {
  graph: MultiDirectedGraph;
  truncated: boolean;
};

function nodeKey(value: any): string {
  return String(value.elementId ?? value.identity);
}

function edgeKey(value: any): string {
  return String(value.elementId ?? value.identity);
}

function isNode(value: any): boolean {
  return Array.isArray(value?.labels) && value?.properties !== undefined && value?.elementId !== undefined;
}

function isRelationship(value: any): boolean {
  return (
    typeof value?.type === 'string' &&
    value?.properties !== undefined &&
    value?.elementId !== undefined &&
    (value?.startNodeElementId !== undefined || value?.start !== undefined)
  );
}

function isPath(value: any): boolean {
  return Array.isArray(value?.segments) || (Array.isArray(value?.nodes) && Array.isArray(value?.relationships));
}

function hslToHex(hue: number, saturation: number, lightness: number): string {
  const h = hue / 360;
  const q = lightness < 0.5 ? lightness * (1 + saturation) : lightness + saturation - lightness * saturation;
  const p = 2 * lightness - q;
  const channel = (t: number): number => {
    let value = t;
    if (value < 0) value += 1;
    if (value > 1) value -= 1;
    if (value < 1 / 6) return p + (q - p) * 6 * value;
    if (value < 1 / 2) return q;
    if (value < 2 / 3) return p + (q - p) * (2 / 3 - value) * 6;
    return p;
  };
  const toHex = (value: number): string => Math.round(value * 255).toString(16).padStart(2, '0');
  return `#${toHex(channel(h + 1 / 3))}${toHex(channel(h))}${toHex(channel(h - 1 / 3))}`;
}

export function colorForLabel(label: string | undefined): string {
  let hash = 0;
  for (const character of label ?? '') hash = (hash * 31 + character.charCodeAt(0)) >>> 0;
  return hslToHex(hash % 360, 0.62, 0.58);
}

function scalarCaption(value: unknown): string | null {
  if (typeof value === 'string') {
    const text = value.trim();
    return text.length > 0 ? text : null;
  }
  if (typeof value === 'number' || typeof value === 'boolean' || typeof value === 'bigint') {
    return String(value);
  }
  return null;
}

/** Generic caption priority, matching Neo4j Browser's default behaviour:
 * name/title/label, then any property whose key ends in "name", then
 * description, then the first scalar property. No schema-specific fields. */
function displayLabel(properties: Record<string, any>, labels: string[], id: string): string {
  const entries = Object.entries(properties);
  const candidates = [
    entries.find(([key]) => /^(name|title|label)$/i.test(key)),
    entries.find(([key]) => /name$/i.test(key)),
    entries.find(([key]) => /^description$/i.test(key)),
    ...entries,
  ];
  for (const candidate of candidates) {
    if (!candidate) continue;
    const text = scalarCaption(candidate[1]);
    if (text !== null) return text.slice(0, 48);
  }
  if (labels.length > 0) return `:${labels[0]} ${id}`.slice(0, 48);
  return `#${id}`;
}

function nodeSize(caption: string): number {
  // Graph-space radius; the graph is rendered with itemSizesReference
  // "positions", so this scales together with the camera.
  return Math.max(9, Math.min(22, 6 + caption.length * 1.25));
}

function canvasCaption(caption: string): string {
  return caption.length <= 8 ? caption : `${caption.slice(0, 7)}…`;
}

/** Extract graph elements from query records. Deduplicates by element id and caps
 * the result so a wide query cannot freeze the browser. */
export function extractGraph(records: any[]): GraphResult {
  const graph = new MultiDirectedGraph();
  let truncated = false;

  const addPlaceholder = (id: string): boolean => {
    if (graph.hasNode(id)) return true;
    if (graph.order >= GRAPH_LIMITS.nodes) {
      truncated = true;
      return false;
    }
    graph.addNode(id, {
      x: Math.random(),
      y: Math.random(),
      size: 12,
      color: '#596173',
      label: `#${id}`,
      caption: `#${id}`,
      labelColor: '#f7f9ff',
      labelSize: 9,
      labelPosition: 'over',
      labels: [],
      properties: {},
      elementId: id,
      placeholder: true,
    });
    return true;
  };

  const addNode = (value: any): void => {
    const id = nodeKey(value);
    const labels = Array.isArray(value.labels) ? value.labels.map(String) : [];
    const properties = value.properties ?? {};
    const caption = displayLabel(properties, labels, id);
    const attributes = {
      x: Math.random(),
      y: Math.random(),
      size: nodeSize(caption),
      color: colorForLabel(labels[0]),
      label: canvasCaption(caption),
      caption,
      labelColor: '#f7f9ff',
      labelSize: 9,
      labelPosition: 'over',
      labels,
      properties,
      elementId: id,
      placeholder: false,
    };

    if (graph.hasNode(id)) {
      // A relationship may have created a placeholder before the actual node
      // showed up; fill in the real data without losing the layout position.
      graph.mergeNodeAttributes(id, attributes);
      return;
    }
    if (graph.order >= GRAPH_LIMITS.nodes) {
      truncated = true;
      return;
    }
    graph.addNode(id, attributes);
  };

  const addEdge = (value: any): void => {
    const id = edgeKey(value);
    if (graph.hasEdge(id)) return;
    if (graph.size >= GRAPH_LIMITS.edges) {
      truncated = true;
      return;
    }

    const source = String(value.startNodeElementId ?? value.start);
    const target = String(value.endNodeElementId ?? value.end);
    if (source === 'undefined' || target === 'undefined') return;
    if (!addPlaceholder(source) || !addPlaceholder(target)) return;

    graph.addEdgeWithKey(id, source, target, {
      label: value.type ?? '',
      color: '#9aa4b8',
      size: 1.2,
      labelSize: 9,
      type: value.type ?? '',
      properties: value.properties ?? {},
    });
  };

  const visit = (value: any): void => {
    if (value === null || value === undefined) return;
    if (typeof value !== 'object') return;

    if (isNode(value)) {
      addNode(value);
      return;
    }
    if (isRelationship(value)) {
      addEdge(value);
      return;
    }
    if (isPath(value)) {
      for (const segment of value.segments ?? []) {
        visit(segment.start);
        visit(segment.end);
        visit(segment.relationship);
      }
      for (const node of value.nodes ?? []) visit(node);
      for (const relationship of value.relationships ?? []) visit(relationship);
      return;
    }
    if (Array.isArray(value)) {
      for (const item of value) visit(item);
      return;
    }
    for (const item of Object.values(value)) visit(item);
  };

  for (const record of records) {
    for (const key of record.keys) visit(record.get(key));
  }

  return { graph, truncated };
}
