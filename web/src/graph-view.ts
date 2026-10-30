import { MultiDirectedGraph } from 'graphology';
import Sigma, { DEFAULT_STYLES } from 'sigma';
import { DEFAULT_PRIMITIVES } from 'sigma/primitives';
import { extremityArrow } from 'sigma/rendering';
import { truncateCaptionToWidth } from './caption';
import { formatValue, orderedPropertyEntries } from './format';
import type { GraphResult } from './graph-data';
import { layoutGraph } from './layout';

type PropertyRow = [string, unknown];

export class GraphView {
  private readonly container: HTMLElement;
  private readonly inspector: HTMLElement;
  private readonly inspectorTitle: HTMLElement;
  private readonly inspectorBody: HTMLElement;
  private readonly notice: HTMLElement;
  private readonly measureContext: CanvasRenderingContext2D | null;
  private sigma: any | null = null;
  private readonly cameraHandler = (): void => this.onCameraUpdated();
  private fitPixelsPerUnit: number | null = null;
  private nodeVisualScale = 1;
  private denseGraph = false;
  private currentZoom = 1;
  private pendingZoom = 1;
  private scaleFrame: number | null = null;

  constructor(container: HTMLElement) {
    this.container = container;
    this.measureContext = document.createElement('canvas').getContext('2d');

    this.inspectorTitle = document.createElement('div');
    this.inspectorTitle.className = 'graph-inspector-title';

    const close = document.createElement('button');
    close.className = 'graph-inspector-close';
    close.type = 'button';
    close.title = '关闭属性面板';
    close.textContent = '×';
    close.addEventListener('click', () => this.hideInspector());

    const header = document.createElement('div');
    header.className = 'graph-inspector-header';
    header.append(this.inspectorTitle, close);

    this.inspectorBody = document.createElement('div');
    this.inspectorBody.className = 'graph-inspector-body';

    this.inspector = document.createElement('div');
    this.inspector.className = 'graph-inspector hidden';
    this.inspector.append(header, this.inspectorBody);

    this.notice = document.createElement('div');
    this.notice.className = 'graph-notice hidden';
    container.append(this.inspector, this.notice);
  }

  render(result: GraphResult): boolean {
    this.destroy();
    if (result.graph.order === 0) {
      this.container.append(this.inspector);
      return false;
    }

    const order = result.graph.order;
    try {
      layoutGraph(result.graph);
    } catch (error) {
      console.warn('Graph layout failed', error);
    }

    // Keep layout spacing but draw denser graphs with smaller marks.
    this.denseGraph = order > 300;
    this.nodeVisualScale = order <= 3 ? 1.8 : order > 300 ? 0.85 : 0.65;
    result.graph.forEachNode((node, attributes) => {
      result.graph.setNodeAttribute(
        node,
        'size',
        ((attributes.size as number) ?? 10) * this.nodeVisualScale
      );
    });
    if (order > 300) {
      result.graph.forEachEdge((edge, attributes) => {
        result.graph.setEdgeAttribute(edge, 'size', ((attributes.size as number) ?? 1.2) * 0.8);
        result.graph.setEdgeAttribute(edge, 'color', '#6b7280');
      });
    }

    try {
      this.sigma = new Sigma(result.graph as any, this.container, {
        primitives: {
          ...DEFAULT_PRIMITIVES,
          edges: {
            ...DEFAULT_PRIMITIVES.edges,
            extremities: [extremityArrow({ lengthRatio: 2.5, widthRatio: 2.0 })],
          },
        },
        settings: {
          renderEdgeLabels: result.graph.size <= 400,
          labelRenderedSizeThreshold: 0,
          enableEdgeEvents: true,
          itemSizesReference: 'positions',
          autoRescale: order > 3,
          minCameraRatio: 0.2,
          maxCameraRatio: 8,
        },
        styles: {
          nodes: [
            {
              ...DEFAULT_STYLES.nodes,
              labelColor: '#f7f9ff',
              labelSize: { attribute: 'labelSize' },
              labelPosition: 'over',
              labelDepth: 'topNodes',
              labelVisibility: order <= 200 ? 'visible' : 'auto',
            },
          ],
          edges: [
            {
              ...DEFAULT_STYLES.edges,
              head: 'arrow',
              labelColor: '#c8d2e6',
              labelSize: { attribute: 'labelSize' },
            },
          ],
        },
      });
    } catch (error) {
      const message = document.createElement('div');
      message.className = 'empty';
      message.textContent = `图视图初始化失败：${String(error)}`;
      this.container.append(message);
      return false;
    }

    if (order > 250) {
      this.notice.textContent = `${order} 节点 · ${result.graph.size} 关系 · 标签按密度隐藏，滚轮放大查看`;
      this.notice.classList.remove('hidden');
    } else {
      this.notice.classList.add('hidden');
    }

    this.sigma.on('clickNode', (payload: any) => this.showNode(payload.node));
    this.sigma.on('clickEdge', (payload: any) => this.showEdge(payload.edge));
    this.sigma.on('clickStage', () => this.hideInspector());
    this.fitPixelsPerUnit = null;
    this.currentZoom = 1;
    this.sigma.getCamera().on('updated', this.cameraHandler);
    this.sigma.resize();
    requestAnimationFrame(() => {
      if (!this.sigma) return;
      this.fitPixelsPerUnit = this.sigma.scaleSize(1);
      this.currentZoom = 1;
      this.applyLabelScale(1, true);
    });
    return true;
  }

  resize(): void {
    this.sigma?.resize();
    requestAnimationFrame(() => {
      if (!this.sigma) return;
      this.fitPixelsPerUnit = this.sigma.scaleSize(1);
      this.currentZoom = 1;
      this.applyLabelScale(1, true);
    });
  }

  destroy(): void {
    if (this.scaleFrame !== null) {
      cancelAnimationFrame(this.scaleFrame);
      this.scaleFrame = null;
    }
    if (this.sigma) {
      this.sigma.getCamera().removeListener('updated', this.cameraHandler);
      this.sigma.kill();
      this.sigma = null;
    }
    this.fitPixelsPerUnit = null;
    this.container.replaceChildren(this.inspector, this.notice);
    this.notice.classList.add('hidden');
    this.hideInspector();
  }

  private onCameraUpdated(): void {
    if (!this.sigma || this.fitPixelsPerUnit === null) return;
    const pixelsPerUnit = this.sigma.scaleSize(1);
    if (!Number.isFinite(pixelsPerUnit) || pixelsPerUnit <= 0) return;
    const zoom = Math.max(0.05, Math.min(2.0, pixelsPerUnit / this.fitPixelsPerUnit));
    this.pendingZoom = zoom;
    if (Math.abs(zoom - this.currentZoom) < 0.01) return;
    if (this.scaleFrame !== null) return;
    this.scaleFrame = requestAnimationFrame(() => {
      this.scaleFrame = null;
      this.applyLabelScale(this.pendingZoom, false);
    });
  }

  /** Labels are always screen-space in sigma; keep them proportional to the
   * graph-space node/edge sizes as the camera zooms. */
  private applyLabelScale(zoom: number, force: boolean): void {
    if (!this.sigma) return;
    if (!force && Math.abs(zoom - this.currentZoom) < 0.02) return;
    this.currentZoom = zoom;
    const graph = this.sigma.getGraph() as MultiDirectedGraph;
    const labelSize = Math.max(1, (this.denseGraph ? 6.5 : 8) * zoom);
    const context = this.measureContext;
    if (context) context.font = `${labelSize}px sans-serif`;
    const measure = (text: string): number =>
      context ? context.measureText(text).width : text.length * labelSize * 0.62;
    graph.forEachNode((node, attributes) => {
      const caption: string = attributes.caption ?? attributes.label ?? `#${node}`;
      // Sigma node `size` is a radius; cap the caption to the visible circle
      // diameter (with a small inset) at the current camera scale.
      const screenRadius = this.sigma.scaleSize((attributes.size as number) ?? 10);
      const maxWidth = Math.max(0, screenRadius * 2 * 0.9);
      const label = truncateCaptionToWidth(caption, maxWidth, measure);
      graph.setNodeAttribute(node, 'labelSize', labelSize);
      graph.setNodeAttribute(node, 'label', label);
    });
    graph.forEachEdge((edge) => {
      graph.setEdgeAttribute(edge, 'labelSize', labelSize);
    });
    this.sigma.refresh();
  }

  private endpointText(graph: MultiDirectedGraph, id: string): string {
    const labels: string[] = graph.getNodeAttribute(id, 'labels') ?? [];
    const caption = graph.getNodeAttribute(id, 'caption') ?? graph.getNodeAttribute(id, 'label') ?? `#${id}`;
    const labelText = labels.length > 0 ? `:${labels.join(':')}` : 'node';
    return `(${labelText}) ${caption} (#${id})`;
  }

  private showNode(node: string): void {
    if (!this.sigma) return;
    const graph = this.sigma.getGraph() as MultiDirectedGraph;
    const attributes = graph.getNodeAttributes(node);
    const labels: string[] = attributes.labels ?? [];
    const caption: string = attributes.caption ?? attributes.label ?? `#${node}`;
    const labelText = labels.length > 0 ? `:${labels.join(':')}` : 'node';
    const rows: PropertyRow[] = [
      ['labels', labels.length > 0 ? labels.join(', ') : '—'],
      ['elementId', attributes.elementId ?? node],
      ...orderedPropertyEntries(attributes.properties ?? {}),
    ];
    this.showInspector(`(${labelText}) ${caption}`, rows);
  }

  private showEdge(edge: string): void {
    if (!this.sigma) return;
    const graph = this.sigma.getGraph() as MultiDirectedGraph;
    const attributes = graph.getEdgeAttributes(edge);
    const source = graph.source(edge);
    const target = graph.target(edge);
    const rows: PropertyRow[] = [
      ['type', attributes.type ?? '—'],
      ['from', this.endpointText(graph, source)],
      ['to', this.endpointText(graph, target)],
    ];
    const properties = orderedPropertyEntries(attributes.properties ?? {});
    if (properties.length === 0) {
      rows.push(['properties', '（无属性）']);
    } else {
      rows.push(...properties);
    }
    this.showInspector(`[:${attributes.type ?? ''}]`, rows);
  }

  private showInspector(title: string, rows: PropertyRow[]): void {
    this.inspectorTitle.textContent = title;
    this.inspectorBody.replaceChildren();
    if (rows.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'graph-inspector-empty';
      empty.textContent = '无属性';
      this.inspectorBody.append(empty);
    } else {
      const table = document.createElement('table');
      table.className = 'prop-table';
      for (const [key, value] of rows) {
        const row = document.createElement('tr');
        const keyCell = document.createElement('td');
        keyCell.className = 'prop-key';
        keyCell.textContent = key;
        const valueCell = document.createElement('td');
        valueCell.className = 'prop-val';
        valueCell.textContent = formatValue(value);
        row.append(keyCell, valueCell);
        table.append(row);
      }
      this.inspectorBody.append(table);
    }
    this.inspector.classList.remove('hidden');
  }

  private hideInspector(): void {
    this.inspector.classList.add('hidden');
  }
}
