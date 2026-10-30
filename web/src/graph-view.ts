import { MultiDirectedGraph } from 'graphology';
import Sigma, { DEFAULT_STYLES } from 'sigma';
import { DEFAULT_PRIMITIVES } from 'sigma/primitives';
import { extremityArrow, pathCurved, pathLine } from 'sigma/rendering';
import { formatValue, orderedPropertyEntries } from './format';
import type { GraphResult } from './graph-data';
import { layoutGraph } from './layout';

type PropertyRow = [string, unknown];

const ARROW_LARGE = extremityArrow({ lengthRatio: 10, widthRatio: 8 });
const ARROW_SMALL_BASE = extremityArrow({ lengthRatio: 5, widthRatio: 4 });
const ARROW_SMALL = {
  ...ARROW_SMALL_BASE,
  name: 'arrowSmall',
  glsl: ARROW_SMALL_BASE.glsl.replaceAll('extremity_arrow', 'extremity_arrowSmall'),
};

export class GraphView {
  private readonly container: HTMLElement;
  private readonly inspector: HTMLElement;
  private readonly inspectorTitle: HTMLElement;
  private readonly inspectorBody: HTMLElement;
  private readonly notice: HTMLElement;
  private readonly labelLayer: HTMLElement;
  private selectedNode: string | null = null;
  private selectedOriginalColor: unknown = undefined;
  private selectedOriginalSize: number | null = null;
  private selectedEdge: string | null = null;
  private selectedEdgeOriginalColor: unknown = undefined;
  private selectedEdgeOriginalSize: number | null = null;
  private sigma: any | null = null;
  private readonly cameraHandler = (): void => this.onCameraUpdated();
  private fitPixelsPerUnit: number | null = null;
  private nodeVisualScale = 1;
  private denseGraph = false;
  private currentZoom = 1;
  private pendingZoom = 1;
  private scaleFrame: number | null = null;
  private labelFrame: number | null = null;

  constructor(container: HTMLElement) {
    this.container = container;

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

    this.labelLayer = document.createElement('div');
    this.labelLayer.className = 'graph-label-layer';

    container.append(this.labelLayer, this.inspector, this.notice);
  }

  render(result: GraphResult): boolean {
    this.destroy();
    if (result.graph.order === 0) {
      this.container.append(this.labelLayer, this.inspector);
      return false;
    }

    const order = result.graph.order;
    // Apply the visual sizes before layout: d3-force's link and collision
    // distances are derived from node radii, matching Neo4j's NodeModel.radius.
    this.denseGraph = order > 300;
    this.nodeVisualScale = order <= 3 ? 1.8 : order <= 20 ? 0.65 : 1.25;
    result.graph.forEachNode((node, attributes) => {
      result.graph.setNodeAttribute(
        node,
        'size',
        ((attributes.size as number) ?? 10) * this.nodeVisualScale
      );
    });
    if (order > 300) {
      result.graph.forEachEdge((edge, attributes) => {
        result.graph.setEdgeAttribute(edge, 'size', ((attributes.size as number) ?? 3.2) * 0.75);
        result.graph.setEdgeAttribute(edge, 'color', '#6b7280');
      });
    }

    try {
      layoutGraph(result.graph);
    } catch (error) {
      console.warn('Graph layout failed', error);
    }

    try {
      this.sigma = new Sigma(result.graph as any, this.container, {
        primitives: {
          ...DEFAULT_PRIMITIVES,
          edges: {
            ...DEFAULT_PRIMITIVES.edges,
            paths: [pathLine(), pathCurved({ curvature: 0.22 })],
            extremities: [ARROW_LARGE, ARROW_SMALL],
          },
        },
        settings: {
          renderEdgeLabels: result.graph.size <= 1000,
          labelRenderedSizeThreshold: 0,
          enableEdgeEvents: true,
          edgeLabelAnchors: 'allNodes',
          edgePickingPadding: 34,
          nodePickingPadding: 6,
          itemSizesReference: 'positions',
          autoRescale: order > 3,
          minCameraRatio: 0.005,
          maxCameraRatio: 8,
        },
        styles: {
          nodes: [
            {
              ...DEFAULT_STYLES.nodes,
              labelVisibility: 'hidden',
            },
          ],
          edges: [
            {
              ...DEFAULT_STYLES.edges,
              path: 'curved',
              head: { attribute: 'head' },
              labelColor: '#9ca3af',
              labelSize: { attribute: 'labelSize' },
              labelVisibility: result.graph.size <= 400 ? 'visible' : 'auto',
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

    this.sigma.on('afterRender', () => this.scheduleLabelLayer());
    this.sigma.on('clickNode', (payload: any) => {
      this.selectNode(payload.node);
      this.showNode(payload.node);
    });
    this.sigma.on('clickEdge', (payload: any) => {
      this.clearNodeSelection();
      this.selectEdge(payload.edge);
      this.showEdge(payload.edge);
    });
    this.sigma.on('clickStage', () => {
      this.clearNodeSelection();
      this.clearEdgeSelection();
      this.hideInspector();
    });
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
    if (this.labelFrame !== null) {
      cancelAnimationFrame(this.labelFrame);
      this.labelFrame = null;
    }
    this.labelLayer.replaceChildren();
    if (this.sigma) {
      this.sigma.getCamera().removeListener('updated', this.cameraHandler);
      this.sigma.kill();
      this.sigma = null;
    }
    this.fitPixelsPerUnit = null;
    this.selectedNode = null;
    this.selectedOriginalColor = undefined;
    this.selectedOriginalSize = null;
    this.selectedEdge = null;
    this.selectedEdgeOriginalColor = undefined;
    this.selectedEdgeOriginalSize = null;
    this.container.replaceChildren(this.labelLayer, this.inspector, this.notice);
    this.notice.classList.add('hidden');
    this.hideInspector();
  }

  private onCameraUpdated(): void {
    if (!this.sigma || this.fitPixelsPerUnit === null) return;
    const pixelsPerUnit = this.sigma.scaleSize(1);
    if (!Number.isFinite(pixelsPerUnit) || pixelsPerUnit <= 0) return;
    const zoom = Math.max(0.05, Math.min(12.0, pixelsPerUnit / this.fitPixelsPerUnit));
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
    const baseLabelSize = Math.max(1, (this.denseGraph ? 6.5 : 8) * zoom);
    const head = this.currentZoom < 1.6 ? 'arrowSmall' : 'arrow';
    graph.forEachEdge((edge) => {
      graph.setEdgeAttribute(edge, 'head', head);
      graph.setEdgeAttribute(edge, 'labelSize', Math.min(baseLabelSize, 24));
    });
    this.sigma.refresh();
  }

  private clearNodeSelection(): void {
    if (!this.sigma || !this.selectedNode) {
      this.selectedNode = null;
      this.selectedOriginalColor = undefined;
      this.selectedOriginalSize = null;
      return;
    }
    const graph = this.sigma.getGraph() as MultiDirectedGraph;
    if (graph.hasNode(this.selectedNode)) {
      if (this.selectedOriginalColor !== undefined) {
        graph.setNodeAttribute(this.selectedNode, 'color', this.selectedOriginalColor as string);
      }
      if (this.selectedOriginalSize !== null) {
        graph.setNodeAttribute(this.selectedNode, 'size', this.selectedOriginalSize);
      }
    }
    this.selectedNode = null;
    this.selectedOriginalColor = undefined;
    this.selectedOriginalSize = null;
    this.sigma.refresh();
  }

  private selectNode(node: string): void {
    if (!this.sigma) return;
    this.clearEdgeSelection();
    this.clearNodeSelection();
    const graph = this.sigma.getGraph() as MultiDirectedGraph;
    if (!graph.hasNode(node)) return;
    const attributes = graph.getNodeAttributes(node);
    this.selectedNode = node;
    this.selectedOriginalColor = attributes.color;
    this.selectedOriginalSize = (attributes.size as number) ?? 10;
    graph.setNodeAttribute(node, 'color', '#5b9dff');
    graph.setNodeAttribute(node, 'size', ((attributes.size as number) ?? 10) * 1.18);
    this.sigma.refresh();
    this.scheduleLabelLayer();
  }

  private clearEdgeSelection(): void {
    if (!this.sigma || !this.selectedEdge) {
      this.selectedEdge = null;
      this.selectedEdgeOriginalColor = undefined;
      this.selectedEdgeOriginalSize = null;
      return;
    }
    const graph = this.sigma.getGraph() as MultiDirectedGraph;
    if (graph.hasEdge(this.selectedEdge)) {
      if (this.selectedEdgeOriginalColor !== undefined) {
        graph.setEdgeAttribute(this.selectedEdge, 'color', this.selectedEdgeOriginalColor as string);
      }
      if (this.selectedEdgeOriginalSize !== null) {
        graph.setEdgeAttribute(this.selectedEdge, 'size', this.selectedEdgeOriginalSize);
      }
    }
    this.selectedEdge = null;
    this.selectedEdgeOriginalColor = undefined;
    this.selectedEdgeOriginalSize = null;
    this.sigma.refresh();
  }

  private selectEdge(edge: string): void {
    if (!this.sigma) return;
    this.clearEdgeSelection();
    const graph = this.sigma.getGraph() as MultiDirectedGraph;
    if (!graph.hasEdge(edge)) return;
    const attributes = graph.getEdgeAttributes(edge);
    this.selectedEdge = edge;
    this.selectedEdgeOriginalColor = attributes.color;
    this.selectedEdgeOriginalSize = (attributes.size as number) ?? 3.2;
    graph.setEdgeAttribute(edge, 'color', '#5b9dff');
    graph.setEdgeAttribute(edge, 'size', ((attributes.size as number) ?? 3.2) * 1.8);
    this.sigma.refresh();
  }

  private scheduleLabelLayer(): void {
    if (this.labelFrame !== null || !this.sigma) return;
    this.labelFrame = requestAnimationFrame(() => {
      this.labelFrame = null;
      this.renderLabelLayer();
    });
  }

  /** HTML labels give us the multi-line, circle-fitted captions that Sigma's
   * WebGL label program cannot render. Only visible labels are materialized. */
  private renderLabelLayer(): void {
    if (!this.sigma) {
      this.labelLayer.replaceChildren();
      return;
    }
    const graph = this.sigma.getGraph() as MultiDirectedGraph;
    const order = graph.order;
    const showLabels = order <= 200 || this.currentZoom >= 2.5;
    if (!showLabels) {
      this.labelLayer.replaceChildren();
      return;
    }

    const dimensions = this.sigma.getDimensions();
    const fragment = document.createDocumentFragment();
    let labelCount = 0;
    graph.forEachNode((node, attributes) => {
      if (labelCount >= 300) return;
      const x = Number(attributes.x);
      const y = Number(attributes.y);
      if (!Number.isFinite(x) || !Number.isFinite(y)) return;
      const point = this.sigma.graphToViewport({ x, y });
      if (!Number.isFinite(point.x) || !Number.isFinite(point.y)) return;
      const radius = this.sigma.scaleSize((attributes.size as number) ?? 10);
      if (!Number.isFinite(radius) || radius < 3) return;
      if (point.x < -radius || point.x > dimensions.width + radius) return;
      if (point.y < -radius || point.y > dimensions.height + radius) return;

      const caption = String(attributes.caption ?? attributes.label ?? `#${node}`);
      if (!caption) return;
      const fontSize = Math.max(6, Math.min(14, radius * 0.55));
      const diameter = radius * 2 * 0.9;
      if (diameter < 10) return;
      const maxLines = Math.max(1, Math.floor(diameter / (fontSize * 1.1)));

      const label = document.createElement('div');
      label.className = 'graph-label';
      label.style.left = `${point.x}px`;
      label.style.top = `${point.y}px`;
      label.style.width = `${diameter}px`;
      label.style.height = `${diameter}px`;
      label.style.fontSize = `${fontSize}px`;
      const text = document.createElement('span');
      text.textContent = caption;
      text.style.setProperty('-webkit-line-clamp', String(maxLines));
      label.append(text);
      fragment.append(label);
      labelCount += 1;
    });
    this.labelLayer.replaceChildren(fragment);
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
