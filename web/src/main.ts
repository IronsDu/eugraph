import neo4j from 'neo4j-driver/lib/browser/neo4j-web.esm.min.js';
import {
  formatValue,
  isNodeValue,
  isPathValue,
  isRelationshipValue,
  orderedPropertyEntries,
  recordsToCsv,
  toPlainValue,
} from './format';
import { colorForLabel, extractGraph } from './graph-data';
import type { GraphResult } from './graph-data';
import { GraphView } from './graph-view';
import { parseInput } from './meta';
import { attachCypherAutocomplete } from './autocomplete-ui';
import './style.css';

type Info = {
  product: string;
  version: string;
  bolt: { port: number };
  database: string;
  assets: { hash: string };
  limits: { graphNodes: number; graphEdges: number };
};

type Theme = 'dark' | 'light';
type Tab = 'table' | 'graph' | 'raw';
type TableMode = 'compact' | 'json';

type FrameHandle = {
  root: HTMLElement;
  query: string;
  summaryText: string;
  destroy: () => void;
};

type State = {
  info: Info | null;
  driver: any | null;
  connected: boolean;
  running: boolean;
  stopping: boolean;
  session: any | null;
  database: string;
  auth: { user: string; password: string } | null;
  params: Record<string, any>;
  history: string[];
  frames: FrameHandle[];
  resultsVisible: boolean;
  runToken: number;
};

const HISTORY_KEY = 'eugraph.console.history.v1';
const THEME_KEY = 'eugraph.console.theme.v1';
const MAX_HISTORY = 200;
const TABLE_PAGE_SIZE = 1000;
const MAX_RAW_RECORDS = 500;

const HELP = `可用命令：
:help                    显示本帮助
:clear                   清空结果
:history                 显示最近执行的命令
:use <db>                切换数据库
:dbs                     列出数据库（SHOW DATABASES）
:param name => <expr>    用 Cypher 表达式设置参数
:params                  显示当前参数
:params clear            清空参数
:params {json}           用 JSON 覆盖参数
:explain <cypher>        查看执行计划
:schema                  查看 schema 图（db.schema.visualization）
查询支持用 ; 分隔多条语句；Ctrl/Cmd+Enter 执行。`;

function h<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attrs: Record<string, string> = {},
  ...children: Array<Node | string>
): HTMLElementTagNameMap[K] {
  const element = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (key === 'class') element.className = value;
    else element.setAttribute(key, value);
  }
  for (const child of children) {
    element.append(typeof child === 'string' ? document.createTextNode(child) : child);
  }
  return element;
}

function boltUri(port: number): string {
  let host = location.hostname;
  if (host.includes(':') && !host.startsWith('[')) host = `[${host}]`;
  return `bolt://${host}:${port}`;
}

function loadHistory(): string[] {
  try {
    const raw = localStorage.getItem(HISTORY_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw);
    return Array.isArray(parsed) ? parsed.filter((item) => typeof item === 'string') : [];
  } catch {
    return [];
  }
}

function saveHistory(history: string[]): void {
  try {
    localStorage.setItem(HISTORY_KEY, JSON.stringify(history.slice(-MAX_HISTORY)));
  } catch {
    /* localStorage can be unavailable in private mode */
  }
}

function describeError(error: unknown): string {
  const candidate = error as Record<string, any>;
  if (candidate?.code && candidate?.message) return `${candidate.code}: ${candidate.message}`;
  return String(error);
}

function loadTheme(): Theme {
  try {
    const stored = window.localStorage.getItem(THEME_KEY);
    if (stored === 'light' || stored === 'dark') return stored;
  } catch {
    // localStorage can be unavailable in private/blocked contexts.
  }
  return 'dark';
}

function mount(root: HTMLElement): { setInfo: (info: Info) => void } {
  const state: State = {
    info: null,
    driver: null,
    connected: false,
    running: false,
    stopping: false,
    session: null,
    database: 'default',
    auth: null,
    params: {},
    history: loadHistory(),
    frames: [],
    resultsVisible: true,
    runToken: 0,
  };

  const status = h('span', { class: 'status status-idle' }, '未连接');
  const serverLabel = h('span', { class: 'muted' }, '');
  const databaseLabel = h('span', { class: 'database-label' }, 'graph: default');
  const databaseButton = h(
    'button',
    { class: 'database-button', type: 'button', 'aria-haspopup': 'listbox', 'aria-expanded': 'false' },
    databaseLabel,
    h('span', { class: 'database-caret' }, '▾')
  ) as HTMLButtonElement;
  const databaseMenu = h('div', { class: 'database-menu hidden' });
  const databaseWrap = h('div', { class: 'database-wrap' }, databaseButton, databaseMenu);
  let databaseMenuOpen = false;
  let databaseFetchToken = 0;
  let theme: Theme = loadTheme();
  const themeButton = h('button', { class: 'button small theme-toggle' }, '') as HTMLButtonElement;
  const loginThemeButton = h('button', { class: 'button small theme-toggle' }, '') as HTMLButtonElement;

  function applyTheme(next: Theme, persist: boolean): void {
    theme = next;
    document.documentElement.dataset.theme = theme;
    themeButton.textContent = theme === 'dark' ? '浅色' : '深色';
    themeButton.title = theme === 'dark' ? '切换到浅色主题' : '切换到深色主题';
    loginThemeButton.textContent = themeButton.textContent;
    loginThemeButton.title = themeButton.title;
    if (persist) {
      try {
        window.localStorage.setItem(THEME_KEY, theme);
      } catch {
        // Ignore unavailable localStorage.
      }
    }
  }

  applyTheme(theme, false);

  const username = h('input', {
    class: 'input login-input',
    value: 'neo4j',
    spellcheck: 'false',
    autocomplete: 'username',
  }) as HTMLInputElement;
  const password = h('input', {
    class: 'input login-input',
    type: 'password',
    value: 'eugraph',
    spellcheck: 'false',
    autocomplete: 'current-password',
  }) as HTMLInputElement;
  const connectButton = h('button', { class: 'button primary login-connect' }, '连接') as HTMLButtonElement;
  const loginMessage = h('div', { class: 'login-message hidden' });
  const loginEndpoint = h('code', { class: 'login-endpoint' }, 'bolt://—');
  const loginView = h(
    'div',
    { class: 'login-screen' },
    h(
      'div',
      { class: 'login-card' },
      h(
        'div',
        { class: 'login-card-top' },
        h('div', { class: 'login-brand' }, h('span', { class: 'brand-mark' }, 'E'), h('span', {}, 'EuGraph Console')),
        loginThemeButton
      ),
      h('div', { class: 'login-subtitle' }, '连接到一个 EuGraph 实例'),
      h(
        'div',
        { class: 'login-field' },
        h('span', { class: 'login-label' }, 'Bolt endpoint'),
        loginEndpoint
      ),
      h('label', { class: 'login-field' }, h('span', { class: 'login-label' }, '用户'), username),
      h('label', { class: 'login-field' }, h('span', { class: 'login-label' }, '密码'), password),
      connectButton,
      h('div', { class: 'login-hint' }, '默认密码 eugraph；凭据只保存在本页会话内'),
      loginMessage
    )
  );

  const editor = h('textarea', {
    class: 'editor',
    spellcheck: 'false',
    placeholder: 'MATCH (n) RETURN n LIMIT 100',
  }) as HTMLTextAreaElement;
  editor.value = 'MATCH (n) RETURN n LIMIT 100';

  let completionLabels: string[] = [];
  let completionRelationships: string[] = [];
  let completionProperties: string[] = [];
  const completionSchema = () => ({
    labels: completionLabels,
    relationships: completionRelationships,
    properties: completionProperties,
  });
  attachCypherAutocomplete(editor, completionSchema);

  const summary = h('span', { class: 'muted' }, '');
  const message = h('div', { class: 'message hidden' });
  const runButton = h('button', { class: 'button primary', disabled: '' }, '执行') as HTMLButtonElement;
  const stopButton = h('button', { class: 'button danger', disabled: '' }, '停止') as HTMLButtonElement;
  const toggleResultsButton = h('button', { class: 'button small' }, '收起结果') as HTMLButtonElement;

  const editorPanel = h(
    'section',
    { class: 'panel editor-panel' },
    h(
      'div',
      { class: 'panel-toolbar' },
      h('span', { class: 'panel-title' }, 'Cypher'),
      h('span', { class: 'spacer' }),
      toggleResultsButton,
      stopButton,
      runButton
    ),
    editor,
    h('div', { class: 'execution-summary' }, summary),
    message
  );

  const emptyFrames = h('div', { class: 'empty' }, '连接后执行一条 Cypher 命令');
  const framesContainer = h('div', { class: 'result-frames' }, emptyFrames);
  const frameCount = h('span', { class: 'muted' }, '');
  const clearFramesButton = h('button', { class: 'button small' }, '清空结果') as HTMLButtonElement;
  const closeResultsButton = h(
    'button',
    { class: 'frame-close result-close', title: '关闭结果面板' },
    '×'
  ) as HTMLButtonElement;
  const resultPanel = h(
    'section',
    { class: 'panel result-panel' },
    h(
      'div',
      { class: 'panel-toolbar' },
      h('span', { class: 'panel-title' }, '结果'),
      h('span', { class: 'spacer' }),
      frameCount,
      clearFramesButton,
      closeResultsButton
    ),
    framesContainer
  );
  const mainLayout = h('main', { class: 'layout' }, editorPanel, resultPanel);

  const sidebarGraph = h('div', { class: 'sidebar-db' }, 'graph: default');
  const sidebarStatus = h('div', { class: 'sidebar-status' }, '连接后加载');
  const sidebarLabels = h('div', { class: 'sidebar-list' });
  const sidebarRelationships = h('div', { class: 'sidebar-list' });
  const sidebarRefresh = h('button', { class: 'button small', type: 'button' }, '刷新');
  const sidebarToggleButton = h(
    'button',
    { class: 'sidebar-toggle', type: 'button', title: '收起侧栏', 'aria-label': '收起侧栏' },
    '◀'
  ) as HTMLButtonElement;
  const sidebar = h(
    'aside',
    { class: 'sidebar' },
    h(
      'div',
      { class: 'sidebar-tabs' },
      h('button', { class: 'sidebar-tab active', type: 'button' }, 'Database'),
      h('span', { class: 'spacer' }),
      sidebarRefresh
    ),
    h(
      'div',
      { class: 'sidebar-body' },
      sidebarGraph,
      sidebarStatus,
      h(
        'div',
        { class: 'sidebar-section' },
        h('div', { class: 'sidebar-section-title' }, 'Labels'),
        sidebarLabels
      ),
      h(
        'div',
        { class: 'sidebar-section' },
        h('div', { class: 'sidebar-section-title' }, 'Relationship types'),
        sidebarRelationships
      )
    )
  );
  const appBody = h('div', { class: 'app-body' }, sidebar, mainLayout);

  const endpointLabel = h('span', { class: 'connection-endpoint' }, 'bolt://—');
  const connectionMenuEndpoint = h('code', { class: 'connection-menu-endpoint' }, 'bolt://—');
  const disconnectButton = h('button', { class: 'connection-disconnect', type: 'button' }, '断开连接') as HTMLButtonElement;
  const connectionMenu = h(
    'div',
    { class: 'connection-menu hidden' },
    h('div', { class: 'connection-menu-header' }, h('span', {}, '当前连接'), status),
    connectionMenuEndpoint,
    serverLabel,
    disconnectButton
  );
  const connectionButton = h(
    'button',
    { class: 'connection-button', type: 'button', 'aria-haspopup': 'menu', 'aria-expanded': 'false' },
    h('span', { class: 'connection-dot' }),
    endpointLabel,
    h('span', { class: 'connection-caret' }, '▾')
  ) as HTMLButtonElement;
  const connectionWrap = h('div', { class: 'connection-wrap' }, connectionButton, connectionMenu);
  let connectionMenuOpen = false;

  const header = h(
    'header',
    { class: 'topbar' },
    h('div', { class: 'brand' }, sidebarToggleButton, h('span', { class: 'brand-mark' }, 'E'), ' EuGraph Console'),
    h('div', { class: 'topbar-meta' }, themeButton, databaseWrap, connectionWrap)
  );
  const appView = h('div', { class: 'app-view hidden' }, header, appBody);

  const inspectorTitle = h('h3', { class: 'modal-title' });
  const inspectorPre = h('pre', { class: 'modal-json' });
  const inspectorCopy = h('button', { class: 'button small' }, '复制 JSON') as HTMLButtonElement;
  const inspectorClose = h('button', { class: 'button' }, '关闭') as HTMLButtonElement;
  const inspectorOverlay = h(
    'div',
    { class: 'modal-overlay hidden' },
    h(
      'div',
      { class: 'modal' },
      h('div', { class: 'modal-header' }, inspectorTitle, h('div', { class: 'modal-actions' }, inspectorCopy, inspectorClose)),
      inspectorPre
    )
  );

  const toast = h('div', { class: 'toast hidden' });
  let toastTimer: number | undefined;

  root.append(loginView, appView, inspectorOverlay, toast);

  function showToast(text: string, kind: 'success' | 'info' | 'error' = 'info'): void {
    toast.textContent = text;
    toast.className = `toast ${kind}`;
    if (toastTimer !== undefined) window.clearTimeout(toastTimer);
    toastTimer = window.setTimeout(() => {
      toast.className = 'toast hidden';
    }, 3200);
  }

  function updateResultsButton(): void {
    if (state.resultsVisible) {
      toggleResultsButton.textContent = '收起结果';
    } else {
      toggleResultsButton.textContent = state.frames.length > 0 ? `显示结果 (${state.frames.length})` : '显示结果';
    }
  }

  function setResultsVisible(visible: boolean): void {
    state.resultsVisible = visible;
    resultPanel.classList.toggle('hidden', !visible);
    mainLayout.classList.toggle('results-hidden', !visible);
    updateResultsButton();
  }

  function closeConnectionMenu(): void {
    connectionMenuOpen = false;
    connectionMenu.classList.add('hidden');
    connectionButton.setAttribute('aria-expanded', 'false');
  }

  function openConnectionMenu(): void {
    connectionMenuOpen = true;
    connectionMenu.classList.remove('hidden');
    connectionButton.setAttribute('aria-expanded', 'true');
  }

  function closeDatabaseMenu(): void {
    databaseMenuOpen = false;
    databaseFetchToken += 1;
    databaseMenu.classList.add('hidden');
    databaseButton.setAttribute('aria-expanded', 'false');
  }

  function applyDatabase(name: string, announce: boolean): void {
    state.database = name;
    databaseLabel.textContent = `graph: ${name}`;
    if (announce) setMessage(`已切换到 graph '${name}'`, 'info');
    closeDatabaseMenu();
    void refreshSidebar();
  }

  function renderDatabaseMenu(
    entries: Array<{ name: string; status: string; current: boolean }>,
    message?: string
  ): void {
    const refresh = h('button', { class: 'button small', type: 'button' }, '刷新');
    refresh.addEventListener('click', (event) => {
      event.stopPropagation();
      void refreshDatabaseList();
    });
    const children: Array<Node | string> = [
      h(
        'div',
        { class: 'database-menu-header' },
        h('span', {}, '切换 Graph / Database'),
        h('span', { class: 'spacer' }),
        refresh
      ),
    ];
    if (message) children.push(h('div', { class: 'database-menu-message' }, message));
    if (entries.length === 0) {
      children.push(h('div', { class: 'database-menu-message' }, '暂无 database'));
    }
    for (const entry of entries) {
      const item = h(
        'button',
        {
          class: `database-item${entry.name === state.database ? ' current' : ''}`,
          type: 'button',
          role: 'option',
          'aria-selected': entry.name === state.database ? 'true' : 'false',
        },
        h('span', { class: 'database-item-check' }, entry.name === state.database ? '✓' : ''),
        h('span', { class: 'database-item-name' }, entry.name),
        h('span', { class: 'database-item-status' }, entry.status || '')
      ) as HTMLButtonElement;
      item.addEventListener('click', (event) => {
        event.stopPropagation();
        applyDatabase(entry.name, true);
      });
      children.push(item);
    }
    databaseMenu.replaceChildren(...children);
  }

  async function refreshDatabaseList(): Promise<void> {
    if (!state.driver) {
      renderDatabaseMenu([], '连接后才能读取 database 列表');
      return;
    }
    const token = ++databaseFetchToken;
    renderDatabaseMenu([], '正在读取最新 database…');
    try {
      const result = await state.driver.executeQuery('SHOW DATABASES', {}, { database: state.database });
      if (token !== databaseFetchToken || !databaseMenuOpen) return;
      const entries = result.records.map((record: any) => ({
        name: String(record.get('name')),
        status: String(record.get('status') ?? record.get('currentStatus') ?? ''),
        current: Boolean(record.get('current')),
      }));
      renderDatabaseMenu(entries);
    } catch (error) {
      if (token !== databaseFetchToken || !databaseMenuOpen) return;
      renderDatabaseMenu([], `读取失败：${describeError(error)}`);
    }
  }

  function openDatabaseMenu(): void {
    databaseMenuOpen = true;
    databaseMenu.classList.remove('hidden');
    databaseButton.setAttribute('aria-expanded', 'true');
    void refreshDatabaseList();
  }

  let sidebarFetchToken = 0;

  function setSidebarOpen(open: boolean): void {
    appBody.classList.toggle('sidebar-collapsed', !open);
    sidebarToggleButton.textContent = open ? '◀' : '▶';
    sidebarToggleButton.title = open ? '收起侧栏' : '展开侧栏';
    sidebarToggleButton.setAttribute('aria-label', sidebarToggleButton.title);
  }

  function clearSidebar(message: string): void {
    sidebarFetchToken += 1;
    sidebarGraph.textContent = `graph: ${state.database}`;
    sidebarStatus.textContent = message;
    sidebarLabels.replaceChildren();
    sidebarRelationships.replaceChildren();
    completionLabels = [];
    completionRelationships = [];
    completionProperties = [];
  }

  function cypherIdentifier(name: string): string {
    return /^[A-Za-z_][A-Za-z0-9_]*$/.test(name)
      ? name
      : `\`${name.replaceAll('`', '``')}\``;
  }

  function renderSidebarList(
    container: HTMLElement,
    items: string[],
    kind: 'label' | 'relationship'
  ): void {
    container.replaceChildren();
    if (items.length === 0) {
      container.append(h('div', { class: 'sidebar-empty' }, '无'));
      return;
    }
    for (const item of items) {
      const button = h('button', { class: 'sidebar-item', type: 'button', title: item }, item);
      button.addEventListener('click', () => {
        const identifier = cypherIdentifier(item);
        editor.value =
          kind === 'label'
            ? `MATCH (n:${identifier}) RETURN n LIMIT 100`
            : `MATCH (a)-[r:${identifier}]->(b) RETURN a, r, b LIMIT 100`;
        editor.focus();
        editor.setSelectionRange(0, editor.value.length);
      });
      container.append(button);
    }
  }

  async function refreshSidebar(): Promise<void> {
    const token = ++sidebarFetchToken;
    sidebarGraph.textContent = `graph: ${state.database}`;
    if (!state.driver) {
      clearSidebar('未连接');
      return;
    }
    sidebarStatus.textContent = '读取 schema…';
    try {
      const [labelsResult, relationshipsResult, propertiesResult] = await Promise.all([
        state.driver.executeQuery(
          'CALL db.labels() YIELD label RETURN label ORDER BY label',
          {},
          { database: state.database }
        ),
        state.driver.executeQuery(
          'CALL db.relationshipTypes() YIELD relationshipType RETURN relationshipType ORDER BY relationshipType',
          {},
          { database: state.database }
        ),
        state.driver.executeQuery(
          'CALL db.propertyKeys() YIELD propertyKey RETURN propertyKey ORDER BY propertyKey',
          {},
          { database: state.database }
        ),
      ]);
      if (token !== sidebarFetchToken) return;
      const labels = labelsResult.records.map((record: any) => String(record.get('label')));
      const relationships = relationshipsResult.records.map((record: any) =>
        String(record.get('relationshipType'))
      );
      completionLabels = labels;
      completionRelationships = relationships;
      completionProperties = propertiesResult.records.map((record: any) =>
        String(record.get('propertyKey'))
      );
      renderSidebarList(sidebarLabels, labels, 'label');
      renderSidebarList(sidebarRelationships, relationships, 'relationship');
      sidebarStatus.textContent = `${labels.length} labels · ${relationships.length} relationship types`;
    } catch (error) {
      if (token !== sidebarFetchToken) return;
      sidebarStatus.textContent = `读取失败：${describeError(error)}`;
    }
  }

  function showInspector(column: string, value: unknown): void {
    const kind = isNodeValue(value)
      ? 'Node'
      : isRelationshipValue(value)
        ? 'Relationship'
        : isPathValue(value)
          ? 'Path'
          : 'Value';
    inspectorTitle.textContent = `${column} · ${kind}`;
    try {
      inspectorPre.textContent = JSON.stringify(toPlainValue(value), null, 2);
    } catch (error) {
      inspectorPre.textContent = String(error);
    }
    inspectorCopy.textContent = '复制 JSON';
    inspectorOverlay.classList.remove('hidden');
  }

  function hideInspector(): void {
    inspectorOverlay.classList.add('hidden');
  }

  inspectorClose.addEventListener('click', hideInspector);
  inspectorCopy.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(inspectorPre.textContent ?? '');
      inspectorCopy.textContent = '已复制';
      setTimeout(() => {
        inspectorCopy.textContent = '复制 JSON';
      }, 1200);
    } catch {
      inspectorCopy.textContent = '复制失败';
    }
  });
  inspectorOverlay.addEventListener('click', (event) => {
    if (event.target === inspectorOverlay) hideInspector();
  });
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && !inspectorOverlay.classList.contains('hidden')) hideInspector();
  });

  function setStatus(text: string, kind: 'ok' | 'error' | 'idle'): void {
    status.textContent = text;
    status.className = `status status-${kind}`;
  }

  function setMessage(text: string, kind: 'error' | 'info'): void {
    message.textContent = text;
    message.className = `message ${kind}`;
  }

  function clearMessage(): void {
    message.textContent = '';
    message.className = 'message hidden';
  }

  function setLoginMessage(text: string, kind: 'error' | 'info'): void {
    loginMessage.textContent = text;
    loginMessage.className = `login-message ${kind}`;
  }

  function clearLoginMessage(): void {
    loginMessage.textContent = '';
    loginMessage.className = 'login-message hidden';
  }

  function setEndpoint(port: number): void {
    const uri = boltUri(port);
    loginEndpoint.textContent = uri;
    endpointLabel.textContent = uri;
    connectionMenuEndpoint.textContent = uri;
  }

  function syncControls(): void {
    const busy = state.running;
    connectButton.disabled = state.connected || busy;
    disconnectButton.disabled = !state.connected;
    username.disabled = state.connected;
    password.disabled = state.connected;
    editor.disabled = !state.connected || busy;
    runButton.disabled = !state.connected || busy;
    stopButton.disabled = !busy;
  }

  function setConnected(connected: boolean): void {
    state.connected = connected;
    loginView.classList.toggle('hidden', connected);
    appView.classList.toggle('hidden', !connected);
    syncControls();
    if (connected) {
      requestAnimationFrame(() => editor.focus());
    }
  }

  async function createDriver(user: string, pass: string): Promise<any> {
    if (!state.info) throw new Error('server info is not loaded yet');
    const driver = neo4j.driver(boltUri(state.info.bolt.port), neo4j.auth.basic(user, pass), {
      disableLosslessIntegers: true,
      maxConnectionPoolSize: 4,
      connectionTimeout: 5000,
      maxTransactionRetryTime: 5000,
    });
    try {
      const serverInfo = await driver.getServerInfo();
      serverLabel.textContent = `${serverInfo.agent ?? ''} ${serverInfo.protocolVersion ?? ''}`.trim();
    } catch (error) {
      await driver.close().catch(() => undefined);
      throw error;
    }
    return driver;
  }

  async function connect(): Promise<void> {
    if (!state.info) {
      setLoginMessage('正在加载 /api/info，请稍候…', 'info');
      return;
    }
    clearLoginMessage();
    clearMessage();
    setStatus('连接中…', 'idle');
    syncControls();
    try {
      const driver = await createDriver(username.value, password.value);
      state.driver = driver;
      state.auth = { user: username.value, password: password.value };
      state.database = state.info.database;
      setEndpoint(state.info.bolt.port);
      applyDatabase(state.database, false);
      setConnected(true);
      setStatus('已连接', 'ok');
      clearLoginMessage();
      showToast(`已连接到 EuGraph · Bolt ${state.info.bolt.port}`, 'success');
    } catch (error) {
      state.driver = null;
      setSidebarOpen(true);
  setConnected(false);
      setStatus('连接失败', 'error');
      setLoginMessage(describeError(error), 'error');
      password.focus();
    }
  }

  async function disconnect(): Promise<void> {
    closeConnectionMenu();
    closeDatabaseMenu();
    const driver = state.driver;
    state.driver = null;
    state.auth = null;
    clearFrames();
    setSidebarOpen(true);
  setConnected(false);
    setStatus('未连接', 'idle');
    serverLabel.textContent = '';
    setLoginMessage('已断开连接', 'info');
    clearSidebar('未连接');
    if (driver) await driver.close().catch(() => undefined);
    username.focus();
  }

  async function stop(): Promise<void> {
    if (!state.running) return;
    state.stopping = true;
    setMessage('正在停止查询（发送 RESET）…', 'info');
    setStatus('停止中…', 'idle');
    const session = state.session;
    const closed = session
      ? await Promise.race([
          session.close().then(() => true).catch(() => true),
          new Promise<boolean>((resolve) => setTimeout(() => resolve(false), 3000)),
        ])
      : true;

    if (!closed) {
      // A single blocking operator (e.g. an aggregate over a huge UNWIND) may
      // not observe the cancel flag. Invalidate the run, drop the driver and
      // reconnect so the UI stays usable; the server may still finish the
      // abandoned statement.
      state.runToken += 1;
      state.running = false;
      state.stopping = false;
      state.session = null;
      runButton.textContent = '执行';
      const oldDriver = state.driver;
      state.driver = null;
      state.connected = false;
      syncControls();
      if (oldDriver) {
        await Promise.race([
          oldDriver.close().catch(() => undefined),
          new Promise((resolve) => setTimeout(resolve, 1000)),
        ]);
      }
      if (state.info && state.auth) {
        try {
          state.driver = await createDriver(state.auth.user, state.auth.password);
          state.connected = true;
          setStatus('已连接', 'ok');
          setMessage('停止超时：已断开并重连；阻塞型查询可能仍在服务端完成', 'error');
        } catch (error) {
          state.driver = null;
          state.connected = false;
          setStatus('重连失败', 'error');
          setMessage(describeError(error), 'error');
        }
      }
      syncControls();
    }
  }

  function previewValue(value: any): string {
    if (value === null || value === undefined) return 'null';
    if (typeof value === 'string') return JSON.stringify(value);
    if (typeof value === 'number' || typeof value === 'boolean' || typeof value === 'bigint') {
      return String(value);
    }
    if (Array.isArray(value)) {
      const shown = value.slice(0, 4).map(previewValue);
      if (value.length > shown.length) shown.push('…');
      return `[${shown.join(', ')}]`;
    }
    try {
      const json = JSON.stringify(value);
      return json === undefined ? String(value) : json;
    } catch {
      return String(value);
    }
  }

  function labelChips(labels: string[]): HTMLElement {
    const wrap = h('span', { class: 'entity-labels' });
    const values = labels.length > 0 ? labels : ['node'];
    for (const label of values) {
      const chip = h('span', { class: 'label-chip' }, label);
      chip.style.setProperty('--chip-color', colorForLabel(label));
      wrap.append(chip);
    }
    return wrap;
  }

  function propertyPreview(properties: Record<string, any> | undefined): HTMLElement {
    const wrap = h('span', { class: 'entity-props' });
    const entries = orderedPropertyEntries(properties);
    if (entries.length === 0) {
      wrap.textContent = '{}';
      return wrap;
    }
    entries.slice(0, 6).forEach(([key, value], index) => {
      if (index > 0) wrap.append(', ');
      wrap.append(h('span', { class: 'prop-key' }, key), ': ', h('span', { class: 'prop-val' }, previewValue(value)));
    });
    if (entries.length > 6) wrap.append(', …');
    return wrap;
  }

  function objectCell(value: any): HTMLElement {
    if (isNodeValue(value)) {
      return h('span', { class: 'entity entity-node' }, labelChips(value.labels ?? []), propertyPreview(value.properties));
    }
    if (isRelationshipValue(value)) {
      return h(
        'span',
        { class: 'entity entity-rel' },
        h('span', { class: 'rel-arrow' }, '→'),
        labelChips([value.type]),
        propertyPreview(value.properties)
      );
    }
    if (isPathValue(value)) {
      return h('span', { class: 'entity entity-path' }, formatValue(value));
    }
    return h('span', { class: 'entity entity-value' }, formatValue(value));
  }

  const MAX_FRAMES = 20;

  function summarizeResult(records: any[], resultSummary: any): string {
    const parts = [`${records.length} 行`];
    if (resultSummary?.resultAvailableAfter !== undefined) {
      parts.push(`首行 ${resultSummary.resultAvailableAfter} ms`);
    }
    if (resultSummary?.resultConsumedAfter !== undefined) {
      parts.push(`完成 ${resultSummary.resultConsumedAfter} ms`);
    }
    return parts.join(' · ');
  }

  function displayQuery(query: string): string {
    const oneLine = query.replace(/\s+/g, ' ').trim();
    return oneLine.length > 160 ? `${oneLine.slice(0, 159)}…` : oneLine || '（空语句）';
  }

  function frameHeader(query: string, summaryText: string): {
    header: HTMLElement;
    edit: HTMLButtonElement;
    close: HTMLButtonElement;
    queryElement: HTMLElement;
    summaryElement: HTMLElement;
  } {
    const display = displayQuery(query);
    const queryElement = h('code', { class: 'frame-query' }, display);
    queryElement.title = query;
    const edit = h('button', { class: 'button small' }, '编辑') as HTMLButtonElement;
    const close = h('button', { class: 'frame-close', title: '关闭这个结果' }, '×') as HTMLButtonElement;
    const summaryElement = h('span', { class: 'frame-summary' }, summaryText);
    const header = h(
      'div',
      { class: 'frame-header' },
      queryElement,
      h('span', { class: 'spacer' }),
      summaryElement,
      edit,
      close
    );
    return { header, edit, close, queryElement, summaryElement };
  }

  function registerFrame(handle: FrameHandle): void {
    // Newest result first; state.frames is also newest-first.
    if (state.frames.length >= MAX_FRAMES) {
      state.frames[state.frames.length - 1].destroy();
    }
    state.frames.unshift(handle);
    emptyFrames.remove();
    framesContainer.prepend(handle.root);
    frameCount.textContent = `${state.frames.length} 个结果`;
    summary.textContent = handle.summaryText;
    // A new result must be visible even if the user had collapsed the panel.
    setResultsVisible(true);
    requestAnimationFrame(() => {
      if (!framesContainer.contains(handle.root)) return;
      framesContainer.scrollTop = 0;
    });
  }

  function unregisterFrame(handle: FrameHandle): void {
    const index = state.frames.indexOf(handle);
    if (index >= 0) state.frames.splice(index, 1);
    handle.root.remove();
    frameCount.textContent = state.frames.length > 0 ? `${state.frames.length} 个结果` : '';
    updateResultsButton();
    if (state.frames.length === 0) {
      if (!emptyFrames.parentElement) framesContainer.append(emptyFrames);
      summary.textContent = '';
    } else {
      summary.textContent = state.frames[0].summaryText;
    }
  }

  function createResultFrame(query: string, columns: string[], records: any[], resultSummary: any): FrameHandle {
    let graphResult = extractGraph(records);
    let hasGraph = graphResult.graph.order > 0;
    let summaryText = summarizeResult(records, resultSummary);
    let activeTab: Tab = 'table';
    let tableMode: TableMode = 'compact';
    let tablePage = 0;
    let graphView: GraphView | null = null;
    let destroyed = false;
    let resultVersion = 0;

    const tablePanel = h('div', { class: 'view-panel' });
    const graphPanel = h('div', { class: 'view-panel graph-panel' });
    const rawPanel = h('div', { class: 'view-panel raw-panel' });

    const tabButtons: Record<Tab, HTMLButtonElement> = {
      table: h('button', { class: 'tab active' }, 'Table') as HTMLButtonElement,
      graph: h('button', { class: 'tab', disabled: '' }, 'Graph') as HTMLButtonElement,
      raw: h('button', { class: 'tab' }, 'RAW') as HTMLButtonElement,
    };
    tabButtons.graph.disabled = !hasGraph;
    tabButtons.graph.textContent = hasGraph ? 'Graph' : 'Graph (无点边)';

    const modeSelect = h(
      'select',
      { class: 'select', 'aria-label': '表格格式' },
      h('option', { value: 'compact' }, 'Compact'),
      h('option', { value: 'json' }, 'JSON')
    ) as HTMLSelectElement;
    modeSelect.value = tableMode;
    modeSelect.disabled = records.length === 0;

    const exportButton = h('button', { class: 'button small', disabled: '' }, '导出 CSV') as HTMLButtonElement;
    exportButton.disabled = columns.length === 0 || records.length === 0;

    const { header, edit, close, queryElement, summaryElement } = frameHeader(query, summaryText);
    const toolbar = h(
      'div',
      { class: 'frame-toolbar' },
      tabButtons.table,
      tabButtons.graph,
      tabButtons.raw,
      h('span', { class: 'spacer' }),
      modeSelect,
      exportButton
    );
    const queryEditor = h('textarea', {
      class: 'frame-editor hidden',
      rows: '2',
      spellcheck: 'false',
    }) as HTMLTextAreaElement;
    attachCypherAutocomplete(queryEditor, completionSchema);
    const executeEditButton = h('button', { class: 'button primary small frame-edit-action hidden' }, '执行') as HTMLButtonElement;
    const cancelEditButton = h('button', { class: 'button small frame-edit-action hidden' }, '取消') as HTMLButtonElement;
    const editError = h('div', { class: 'frame-edit-error hidden' });
    header.insertBefore(queryEditor, header.firstChild);
    header.insertBefore(executeEditButton, close);
    header.insertBefore(cancelEditButton, close);
    const body = h('div', { class: 'frame-body' }, tablePanel, graphPanel, rawPanel);
    const root = h('article', { class: 'result-frame' }, header, editError, toolbar, body);

    function renderTable(): void {
      tablePanel.replaceChildren();
      if (columns.length === 0) {
        tablePanel.append(h('div', { class: 'empty' }, 'OK（无返回列）'));
        return;
      }

      const table = h('table', { class: 'result-table' });
      const headRow = h('tr');
      headRow.append(h('th', { class: 'rownum' }, '#'));
      for (const column of columns) headRow.append(h('th', {}, column));
      table.append(h('thead', {}, headRow));

      const tableBody = h('tbody');
      const pageCount = Math.max(1, Math.ceil(records.length / TABLE_PAGE_SIZE));
      tablePage = Math.min(tablePage, pageCount - 1);
      const start = tablePage * TABLE_PAGE_SIZE;
      const end = Math.min(records.length, start + TABLE_PAGE_SIZE);
      for (let index = start; index < end; index += 1) {
        const record = records[index];
        const row = h('tr');
        row.append(h('td', { class: 'rownum' }, String(index + 1)));
        for (const column of columns) {
          const cell = h('td');
          const value = record.get(column);
          const isObject = value !== null && typeof value === 'object';
          if (isObject) {
            cell.classList.add('value-object');
            cell.title = '点击查看完整内容';
            if (tableMode === 'json') {
              const pre = h('pre', { class: 'json-cell' });
              try {
                pre.textContent = JSON.stringify(toPlainValue(value), null, 2);
              } catch (error) {
                pre.textContent = String(error);
              }
              cell.append(pre);
            } else {
              cell.append(objectCell(value));
            }
            cell.addEventListener('click', () => showInspector(column, value));
          } else {
            cell.textContent = formatValue(value);
          }
          row.append(cell);
        }
        tableBody.append(row);
      }
      table.append(tableBody);
      tablePanel.append(table);

      if (pageCount > 1) {
        const previous = h('button', { class: 'button small' }, '上一页') as HTMLButtonElement;
        previous.disabled = tablePage === 0;
        previous.addEventListener('click', () => {
          tablePage = Math.max(0, tablePage - 1);
          renderTable();
        });
        const next = h('button', { class: 'button small' }, '下一页') as HTMLButtonElement;
        next.disabled = tablePage >= pageCount - 1;
        next.addEventListener('click', () => {
          tablePage = Math.min(pageCount - 1, tablePage + 1);
          renderTable();
        });
        tablePanel.append(
          h(
            'div',
            { class: 'table-pager' },
            h('span', { class: 'muted' }, `第 ${start + 1}–${end} 行 / 共 ${records.length} 行 · 每页 ${TABLE_PAGE_SIZE} 行`),
            h('span', { class: 'spacer' }),
            previous,
            next
          )
        );
      }
    }

    function renderRaw(): void {
      rawPanel.replaceChildren();
      if (columns.length === 0) {
        rawPanel.append(h('div', { class: 'empty' }, 'OK（无返回列）'));
        return;
      }
      const rows = records.slice(0, MAX_RAW_RECORDS).map((record) => {
        const plain: Record<string, unknown> = {};
        for (const column of columns) plain[column] = toPlainValue(record.get(column));
        return plain;
      });
      const pre = h('pre', { class: 'raw-json' });
      pre.textContent = JSON.stringify(rows, null, 2);
      rawPanel.append(pre);
      if (records.length > MAX_RAW_RECORDS) {
        rawPanel.append(h('div', { class: 'notice' }, `RAW 仅显示前 ${MAX_RAW_RECORDS} 条（共 ${records.length} 条）`));
      }
    }

    function activate(tab: Tab): void {
      if (tab === 'graph' && !hasGraph) tab = 'table';
      activeTab = tab;
      for (const [name, button] of Object.entries(tabButtons)) {
        button.classList.toggle('active', name === tab);
      }
      tablePanel.classList.toggle('hidden', tab !== 'table');
      graphPanel.classList.toggle('hidden', tab !== 'graph');
      rawPanel.classList.toggle('hidden', tab !== 'raw');

      if (tab === 'graph') {
        if (!graphView) {
          graphView = new GraphView(graphPanel);
          graphView.render(graphResult);
        } else {
          graphView.resize();
        }
      }
    }

    function destroy(): void {
      if (destroyed) return;
      destroyed = true;
      graphView?.destroy();
      graphView = null;
      unregisterFrame(handle);
    }

    const handle: FrameHandle = { root, query, summaryText, destroy };

    function applyResult(
      nextQuery: string,
      nextColumns: string[],
      nextRecords: any[],
      nextSummary: any
    ): void {
      query = nextQuery;
      columns = nextColumns;
      records = nextRecords;
      graphResult = extractGraph(records);
      hasGraph = graphResult.graph.order > 0;
      summaryText = summarizeResult(records, nextSummary);
      handle.query = query;
      handle.summaryText = summaryText;

      queryElement.textContent = displayQuery(query);
      queryElement.title = query;
      summaryElement.textContent = summaryText;
      queryEditor.value = query;
      modeSelect.disabled = records.length === 0;
      exportButton.disabled = columns.length === 0 || records.length === 0;
      tabButtons.graph.disabled = !hasGraph;
      tabButtons.graph.textContent = hasGraph ? 'Graph' : 'Graph (无点边)';

      graphView?.destroy();
      graphView = null;
      graphPanel.replaceChildren();
      tablePage = 0;
      renderTable();
      renderRaw();
      activate('table');
      if (state.frames[0] === handle) summary.textContent = summaryText;

      // Neo4j Browser opens Graph by default for graph-shaped results. Do it
      // after the frame is mounted so Sigma sees a non-zero container size.
      const version = ++resultVersion;
      if (hasGraph) {
        requestAnimationFrame(() => {
          if (destroyed || version !== resultVersion || !root.isConnected) return;
          activate('graph');
        });
      }
    }

    function beginEdit(): void {
      queryEditor.value = query;
      queryEditor.classList.remove('hidden');
      queryElement.classList.add('hidden');
      edit.classList.add('hidden');
      executeEditButton.classList.remove('hidden');
      cancelEditButton.classList.remove('hidden');
      editError.classList.add('hidden');
      header.classList.add('editing');
      requestAnimationFrame(() => {
        queryEditor.focus();
        queryEditor.select();
      });
    }

    function endEdit(): void {
      queryEditor.classList.add('hidden');
      queryElement.classList.remove('hidden');
      edit.classList.remove('hidden');
      executeEditButton.classList.add('hidden');
      cancelEditButton.classList.add('hidden');
      editError.classList.add('hidden');
      header.classList.remove('editing');
      executeEditButton.disabled = false;
      cancelEditButton.disabled = false;
      queryEditor.disabled = false;
      executeEditButton.textContent = '执行';
    }

    async function executeFrame(): Promise<void> {
      const statement = queryEditor.value.trim();
      if (!statement || state.running) return;
      editError.classList.add('hidden');
      state.running = true;
      state.stopping = false;
      executeEditButton.disabled = true;
      cancelEditButton.disabled = true;
      queryEditor.disabled = true;
      executeEditButton.textContent = '执行中…';
      syncControls();
      try {
        const data = await executeQuery(statement);
        applyResult(statement, data.columns, data.records, data.summary);
        endEdit();
        clearMessage();
      } catch (error) {
        if (!state.stopping) {
          editError.textContent = describeError(error);
          editError.classList.remove('hidden');
        }
      } finally {
        state.running = false;
        executeEditButton.disabled = false;
        cancelEditButton.disabled = false;
        queryEditor.disabled = false;
        executeEditButton.textContent = '执行';
        const wasStopping = state.stopping;
        state.stopping = false;
        if (wasStopping && state.driver) setStatus('已连接', 'ok');
        syncControls();
      }
    }

    tabButtons.table.addEventListener('click', () => activate('table'));
    tabButtons.graph.addEventListener('click', () => activate('graph'));
    tabButtons.raw.addEventListener('click', () => activate('raw'));
    modeSelect.addEventListener('change', () => {
      tableMode = modeSelect.value as TableMode;
      tablePage = 0;
      renderTable();
    });
    exportButton.addEventListener('click', () => {
      if (columns.length === 0) return;
      const blob = new Blob([recordsToCsv(columns, records)], { type: 'text/csv;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = 'eugraph-result.csv';
      link.click();
      URL.revokeObjectURL(url);
    });
    close.addEventListener('click', destroy);
    edit.addEventListener('click', beginEdit);
    executeEditButton.addEventListener('click', () => void executeFrame());
    cancelEditButton.addEventListener('click', endEdit);
    queryEditor.addEventListener('keydown', (event) => {
      if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') {
        event.preventDefault();
        void executeFrame();
      } else if (event.key === 'Escape') {
        event.preventDefault();
        endEdit();
      }
    });
    applyResult(query, columns, records, resultSummary);
    return handle;
  }

  function addResultFrame(query: string, columns: string[], records: any[], resultSummary: any): void {
    registerFrame(createResultFrame(query, columns, records, resultSummary));
  }

  function addInfoFrame(query: string, summaryText: string, text: string): void {
    const { header, edit, close } = frameHeader(query, summaryText);
    edit.remove();
    let destroyed = false;
    const root = h(
      'article',
      { class: 'result-frame info-frame' },
      header,
      h('div', { class: 'frame-info' }, text)
    );
    const handle: FrameHandle = {
      root,
      query,
      summaryText,
      destroy: () => {
        if (destroyed) return;
        destroyed = true;
        unregisterFrame(handle);
      },
    };
    close.addEventListener('click', handle.destroy);
    registerFrame(handle);
  }

  function addErrorFrame(query: string, text: string): void {
    const summaryText = '执行失败';
    const { header, edit, close, queryElement, summaryElement } = frameHeader(query, summaryText);
    let destroyed = false;
    const root = h('article', { class: 'result-frame error-frame' }, header, h('div', { class: 'frame-error' }, text));
    const handle: FrameHandle = {
      root,
      query,
      summaryText,
      destroy: () => {
        if (destroyed) return;
        destroyed = true;
        unregisterFrame(handle);
      },
    };
    edit.remove();
    close.addEventListener('click', handle.destroy);
    registerFrame(handle);
  }

  function clearFrames(): void {
    const frames = [...state.frames];
    state.frames = [];
    for (const frame of frames) frame.destroy();
    framesContainer.replaceChildren(emptyFrames);
    frameCount.textContent = '';
    updateResultsButton();
    summary.textContent = '';
  }

  async function executeQuery(statement: string): Promise<{
    columns: string[];
    records: any[];
    summary: any;
  }> {
    if (!state.driver) throw new Error('not connected');
    const session = state.driver.session({ database: state.database });
    state.session = session;
    try {
      const result = await session.run(statement, state.params);
      const records = result.records as any[];
      const columns = records.length > 0 ? (records[0].keys as string[]) : [];
      return { columns, records, summary: result.summary };
    } finally {
      await session.close().catch(() => undefined);
      if (state.session === session) state.session = null;
    }
  }

  async function executeAndRender(statement: string, token: number): Promise<void> {
    const data = await executeQuery(statement);
    if (token !== state.runToken) return;
    addResultFrame(statement, data.columns, data.records, data.summary);
  }

  async function setParameter(args: string): Promise<void> {
    const match = /^([A-Za-z_][A-Za-z0-9_]*)\s*=>\s*([\s\S]+)$/.exec(args);
    if (!match) {
      setMessage('用法：:param name => <Cypher 表达式>（如 :param x => 1）', 'error');
      return;
    }
    if (!state.driver) throw new Error('not connected');
    const result = await state.driver.executeQuery(`RETURN ${match[2]} AS value`, {}, { database: state.database });
    const value = result.records[0]?.get('value');
    state.params[match[1]] = value;
    setMessage(`$${match[1]} = ${formatValue(value)}`, 'info');
  }

  async function handleMeta(command: string, args: string, token: number): Promise<void> {
    switch (command) {
      case 'help':
        clearMessage();
        addInfoFrame(':help', '帮助', HELP);
        return;
      case 'clear':
        clearFrames();
        clearMessage();
        return;
      case 'history': {
        const recent = state.history.slice(-20).reverse();
        setMessage(
          recent.length === 0 ? '暂无历史' : recent.map((item, index) => `${index + 1}. ${item}`).join('\n'),
          'info'
        );
        return;
      }
      case 'use': {
        const database = args.trim();
        if (!database) {
          setMessage('用法：:use <database>', 'error');
          return;
        }
        applyDatabase(database, true);
        return;
      }
      case 'dbs':
        await executeAndRender('SHOW DATABASES', token);
        return;
      case 'param':
        await setParameter(args);
        return;
      case 'params':
        if (args.trim() === 'clear') {
          state.params = {};
          setMessage('参数已清空', 'info');
          return;
        }
        if (args.trim().startsWith('{')) {
          try {
            const parsed = JSON.parse(args);
            if (parsed === null || Array.isArray(parsed) || typeof parsed !== 'object') {
              throw new Error('参数必须是 JSON object');
            }
            state.params = parsed as Record<string, any>;
            setMessage(`已设置参数：\n${JSON.stringify(state.params, null, 2)}`, 'info');
          } catch (error) {
            setMessage(`参数 JSON 无效：${String(error)}`, 'error');
          }
          return;
        }
        setMessage(`当前参数：\n${JSON.stringify(state.params, null, 2)}`, 'info');
        return;
      case 'explain': {
        if (!args.trim()) {
          setMessage('用法：:explain <cypher>', 'error');
          return;
        }
        await executeAndRender(`EXPLAIN ${args}`, token);
        return;
      }
      case 'schema':
        try {
          await executeAndRender('CALL db.schema.visualization()', token);
        } catch {
          await executeAndRender('CALL db.labels() RETURN label', token);
        }
        return;
      default:
        setMessage(`未知命令 :${command}；输入 :help 查看帮助`, 'error');
        return;
    }
  }

  function pushHistory(text: string): void {
    if (!text.trim()) return;
    state.history.push(text.trim());
    if (state.history.length > MAX_HISTORY) state.history = state.history.slice(-MAX_HISTORY);
    saveHistory(state.history);
  }

  async function run(): Promise<void> {
    if (!state.driver || state.running) return;
    const input = editor.value.trim();
    if (!input) return;

    const token = ++state.runToken;
    state.running = true;
    state.stopping = false;
    runButton.textContent = '执行中…';
    syncControls();
    clearMessage();
    summary.textContent = '';

    const parsed = parseInput(input);
    let currentQuery = input;

    try {
      if (parsed.kind === 'meta') {
        pushHistory(input);
        await handleMeta(parsed.command, parsed.args, token);
      } else {
        if (parsed.statements.length === 0) {
          setMessage('没有可执行的语句', 'info');
          return;
        }
        if (parsed.statements.length > 1) {
          setMessage(`正在顺序执行 ${parsed.statements.length} 条语句…`, 'info');
        }
        for (const statement of parsed.statements) {
          if (state.stopping || token !== state.runToken) break;
          currentQuery = statement;
          await executeAndRender(statement, token);
          if (token !== state.runToken) break;
          pushHistory(statement);
        }
      }
    } catch (error) {
      if (token !== state.runToken) return;
      if (state.stopping) {
        setMessage('查询已停止（未完成事务已回滚）', 'info');
      } else {
        addErrorFrame(currentQuery, describeError(error));
        setMessage(describeError(error), 'error');
      }
    } finally {
      if (token !== state.runToken) return;
      state.running = false;
      runButton.textContent = '执行';
      const wasStopping = state.stopping;
      state.stopping = false;
      if (wasStopping && state.driver) {
        setStatus('已连接', 'ok');
      }
      syncControls();
    }
  }

  clearFramesButton.addEventListener('click', clearFrames);
  closeResultsButton.addEventListener('click', () => setResultsVisible(false));
  toggleResultsButton.addEventListener('click', () => setResultsVisible(!state.resultsVisible));
  sidebarToggleButton.addEventListener('click', () => {
    setSidebarOpen(appBody.classList.contains('sidebar-collapsed'));
  });
  sidebarRefresh.addEventListener('click', () => void refreshSidebar());
  themeButton.addEventListener('click', () => applyTheme(theme === 'dark' ? 'light' : 'dark', true));
  loginThemeButton.addEventListener('click', () => applyTheme(theme === 'dark' ? 'light' : 'dark', true));
  connectionButton.addEventListener('click', (event) => {
    event.stopPropagation();
    if (connectionMenuOpen) closeConnectionMenu();
    else openConnectionMenu();
  });
  disconnectButton.addEventListener('click', () => void disconnect());
  databaseButton.addEventListener('click', (event) => {
    event.stopPropagation();
    if (databaseMenuOpen) closeDatabaseMenu();
    else openDatabaseMenu();
  });
  document.addEventListener('click', (event) => {
    if (databaseMenuOpen && !databaseWrap.contains(event.target as Node)) closeDatabaseMenu();
    if (connectionMenuOpen && !connectionWrap.contains(event.target as Node)) closeConnectionMenu();
  });
  document.addEventListener('keydown', (event) => {
    if (event.key !== 'Escape') return;
    if (databaseMenuOpen) closeDatabaseMenu();
    if (connectionMenuOpen) closeConnectionMenu();
  });
  connectButton.addEventListener('click', () => void connect());
  password.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      void connect();
    }
  });
  runButton.addEventListener('click', () => void run());
  stopButton.addEventListener('click', () => void stop());
  editor.addEventListener('keydown', (event) => {
    if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') {
      event.preventDefault();
      void run();
    }
  });

  setSidebarOpen(true);
  setConnected(false);

  return {
    setInfo(info: Info): void {
      state.info = info;
      state.database = info.database;
      document.title = `${info.product} Console ${info.version}`;
      setEndpoint(info.bolt.port);
      applyDatabase(info.database, false);
      clearMessage();
      syncControls();
    },
  };
}

async function boot(): Promise<void> {
  const root = document.getElementById('app');
  if (!root) throw new Error('#app is missing');
  const app = mount(root);
  try {
    const response = await fetch('/api/info', { cache: 'no-store' });
    if (!response.ok) throw new Error(`/api/info returned HTTP ${response.status}`);
    app.setInfo((await response.json()) as Info);
  } catch (error) {
    const loginCard = document.querySelector('.login-card');
    const message = document.createElement('div');
    message.className = 'login-message error';
    message.textContent = `无法读取 /api/info：${String(error)}`;
    loginCard?.append(message);
  }
}

void boot();
