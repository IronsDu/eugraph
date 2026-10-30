# 内嵌 Web Console

> 当前实现状态：内嵌 HTTP 资产 + Bolt-over-WebSocket 控制台。
> 已具备连接、Cypher 执行、Table/Graph/RAW 三视图、`:` 命令、参数、CSV 导出与
> best-effort Stop；Schema 侧栏、自动补全、图布局 Worker 等仍在后续里程碑。

## 1. 目标与边界

EuGraph 的 Web Console 是**编进 `eugraph-server` 的静态单页应用**：

- 一个二进制自带前端资产，部署时不需要额外的 `web/` 目录或 Web 容器；
- 浏览器使用官方 `neo4j-driver`（browser build）通过 **Bolt-over-WebSocket** 执行 Cypher；
- 不新增查询/事务/结果序列化通道，语义与官方驱动完全一致；
- 默认只监听 `127.0.0.1`，不引入外部 CDN、字体或遥测。

## 2. 架构

```
浏览器（Vite 构建的 SPA，资产内嵌）
  ├─ GET /, /assets/*, /api/info  → WebConsoleServer :7475（Drogon，默认）
  └─ Bolt over WebSocket          → BoltServer :7687
                                        → GraphService / QueryExecutor
```

- `WebConsoleServer` 只提供 **GET/HEAD**：内存资产、`/api/info`、`/healthz`。
- 查询走现有 `BoltServer`，不经过 HTTP 层。
- 默认开启，端口 `7475`（避开 Neo4j Browser 的 7474）；`--web-port 0` 关闭。
  Bolt 被禁用时 Web Console 自动关闭（Thrift-only / TCK 场景）。

### 控制台能力

- 顶栏可在深色/浅色主题间切换，选择保存在浏览器 localStorage；默认深色。
- 顶栏 Graph/Database 下拉每次展开都会重新执行 `SHOW DATABASES` 获取最新列表（含 online
  状态，当前项高亮）；选择后只切换后续 session 使用的数据库，已有结果 frame 保留。
- 主界面左侧是可折叠 Sidebar；Database 概览 tab 显示当前 graph 的 labels 和 relationship
  types，切换 database 或点刷新时重新读取；点击列表项会把对应的 MATCH 查询写入编辑器。
- 启动时显示独立连接页（默认 `neo4j` / `eugraph`，凭据只保存在页面会话内）；连接成功后
  进入主界面。顶栏显示当前 Bolt 地址和 graph 名；地址下拉内提供“断开连接”，断开后回到
  登录页。数据库切换可用顶栏下拉或 `:use`。
- 结果包含节点/关系时默认打开 Graph（与 Neo4j Browser 一致），仍可切回 Table/RAW。
- Cypher 编辑器：`Ctrl/Cmd+Enter` 执行，`;` 分隔多语句，顺序执行；返回行数与
  `resultAvailableAfter` / `resultConsumedAfter` 显示在输入框下方。
- 结果：每次执行追加一个独立结果 frame，旧结果保留在下方，不再覆盖；每个 frame 分别拥有
  Table（每页 1000 行，带上一页/下一页）、Graph（Sigma v4 / WebGL2）、RAW、导出 CSV 和关闭。
  每个 frame 的“编辑”会就地展开语句输入框和“执行/取消”；执行成功后**只更新当前 frame**，
  不会新建结果框，失败则在该 frame 内显示错误。输入框下方的摘要显示最新一次查询的总行数。
  结果列表按执行时间倒序排列（最新结果在最上面），最多保留 20 个 frame。整个结果面板可用
  右上角 `×` 收起（编辑区自动
  占满高度），编辑栏的“显示结果 (N)”可重新展开并保留所有 frame；每个 frame 也有自己的 `×`
  只关闭该结果。新结果到达时会自动展开结果面板并滚动到顶部；frame 高度按内容在
  275–510px 之间自适应（Graph 用满高度），避免只露出半截或大片空白。大图会自动缩小节点/标签，
  超过 250 个节点时在图上显示节点/边计数与“标签按密度隐藏”的提示。
- Table 参考 Neo4j Browser 的对象展示：节点/关系显示**标签 + 属性摘要**（`name`/`title` 优先），
  可在 `Compact / JSON` 间切换；点击对象单元格打开完整 JSON 详情（含复制）。
- 图视图：节点/关系去重、多重边/自环、按标签着色；**有向边使用 curved path 并带箭头**；
  节点尺寸在图坐标中定义、随相机整体缩放；节点 caption 由 HTML 叠加层绘制在圆圈内，支持
  多行换行和圆内裁切（Neo4j 风格通用优先级：`name`/`title`/`label` → 任意以 `name` 结尾的
  属性 → `description` → 第一个标量属性；不写死数据集字段）；点击节点在右上角属性面板显示标签、
  elementId 与全部属性；点击边线显示关系类型、from/to（节点标签 + caption + id）与边属性
  （无属性时显示「无属性」）。布局采用 Neo4j 风格的圆形初始位置：结果只有节点时按同心环
  绕中心规则排布；有边且不超过 600 节点时，整张图跑一次 Neo4j 风格力模型
  （charge、link 目标距离 `r1+r2+90`、center、collide `r1+r2+25`），不按分量打包，
  避免小分量把主图挤小；节点会形成内部网络而不是停留在外圈。更大图回退到 ForceAtlas2
  分量布局。
  节点圈、边线、节点文字与边标签都按相机缩放比例同步缩放；HTML caption 受节点圆的宽高和
  最大行数约束，超出部分在圆内裁切。大图在 fit 视图隐藏节点文字，放大后只为视口内节点生成标签。
  默认上限 2000 节点 / 5000 边，超限显示截断提示。
- 命令：`:help`、`:clear`、`:history`、`:use`、`:dbs`、`:param`、`:params`、
  `:params clear`、`:params {json}`、`:explain`、`:schema`。`:help` 显示在独立的信息 frame 中，
  内容区可滚动，不再挤在编辑器下方的 message 里。
- 导出：CSV（当前结果集）、图截图（后续里程碑）。
- Stop：通过 `session.close()` 发送 RESET。服务端在解码到 RESET 时**立即**置取消位，
  正在流式产出的算子在下一个检查点停止并回滚；单条阻断型算子（如超大 `UNWIND` 聚合）
  可能观察不到取消，3 秒后会断开并重连，UI 给出明确提示。

## 3. 前端资产如何进入二进制

```
web/src + package-lock.json
        │  ./scripts/build-web.sh（只在改 UI 时执行）
        ▼
web/dist/（提交入库，Vite 产物带 content hash）
        │  C++ 构建时自动执行 scripts/gen_web_assets.py
        ▼
build/<preset>/web_generated/web_assets.generated.cpp
        │  只重编这一个文件
        ▼
eugraph_web → eugraph-server
```

### 3.1 前端没改（C++ 日常开发 / CI）

- CMake 只检查 `web/dist/index.html`，用 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 收集资产；
- 若 `web_assets.generated.cpp` 比所有 dist 文件新，生成器与编译都会被跳过；
- **不需要 Node/npm**；全新 build 目录只会跑一次生成器。

### 3.2 前端改了

- 日常迭代：`cd web && npm run dev`（5173，HMR），`/api` 代理到 7475，Bolt 直连 7687；**不重编 C++**。
- 集成/发布：`./scripts/build-web.sh` → `cmake --build --preset debug`；只重编生成的资产 `.cpp` 并重新链接。
- CI：`./scripts/build-web.sh --check` 会构建到临时目录并与 `web/dist` 比较，防止“改了 `web/src` 忘记提交 dist”。

### 3.3 生成物

`gen_web_assets.py` 为每个文件生成一个 `constexpr unsigned char[]`，并生成按路径排序的清单：

```cpp
struct WebAsset {
    std::string_view path;         // "/assets/index-abc.js"
    std::string_view content_type;
    std::string_view etag;         // 内容 SHA-256
    std::string_view body;         // 指向 .rodata
    bool compressible;
};
```

运行时按路径精确查表，**不访问文件系统**；未知路径返回 404。

### 3.4 gzip / 缓存

- 启动时用 zlib 对可压缩资产做一次 gzip，放入内存缓存；
- `Accept-Encoding: gzip` → 返回 gzip 变体 + `Content-Encoding: gzip` + `Vary: Accept-Encoding`；
- ETag 分变体：`"<sha>"` / `"<sha>-gzip"`，支持 `If-None-Match` → 304；
- `/assets/*` → `Cache-Control: public, max-age=31536000, immutable`；
- `/`、`/api/info`、`/healthz` → `no-store`。

## 4. 使用

```bash
# 默认同时启动 Bolt 和 Web Console（7475）
eugraph-server --data-dir ./eugraph-data --bolt-port 7687

# 浏览器打开
# http://127.0.0.1:7475

# 关闭 Web Console
eugraph-server --data-dir ./eugraph-data --bolt-port 7687 --web-port 0
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--web-port` | `7475` | Web Console HTTP 端口；`0` 关闭；Bolt 禁用时自动关闭 |
| `--web-bind` | `127.0.0.1` | 监听地址（当前仅 IPv4） |
| `--web-allowed-origin` | 空 | 额外允许的浏览器 WebSocket Origin，可重复；非回环部署时使用 |

## 5. 安全

- **默认仅回环**；非回环部署需要显式 `--web-bind`，并配置 `--web-allowed-origin`。
- **WebSocket Origin 策略**：无 Origin（官方驱动/命令行）放行；浏览器 Origin 仅允许
  `http(s)://localhost[:port]`、`http(s)://127.0.0.1[:port]`、`http(s)://[::1][:port]`
  以及 `--web-allowed-origin` 精确列出的值。外部站点通过浏览器直连 Bolt 会被 403。
- HTTP 层只接受 GET/HEAD，无请求体；资产走精确查表，不落盘、无路径穿越。
- CSP：`script-src 'self'`、`connect-src 'self' ws://<host>:<bolt-port>`、`frame-ancestors 'none'`。
- 浏览器里的数据库凭据对客户端可见（官方驱动文档已提示）；凭据只放在页面内存里，
  不写存储、不写日志；localStorage 只保存查询历史文本和主题选择。Bolt 默认口令仍是 `eugraph`，
  正式环境应先改认证配置。

## 6. 当前限制

- **Stop 是 best-effort**：流式查询可在检查点停止；单条阻断型算子可能跑完（见上）。
- 图布局在主线程同步执行（未使用 Worker）；更大规模请先用 `LIMIT`/标签过滤。
- 无 Schema 侧栏、自动补全、Plan 专用视图、PNG 导出（后续里程碑）。
- 前端 bundle 约 1.10 MB raw / 256 KB gzip（含 neo4j-driver + Sigma）。
- Drogon 是进程级单例：一个进程只能启动一次 Web Console，且 stop 后不能重启；
  集成测试通过独立进程启动。
- trantor 在端口被占用时会 `exit(1)`；`WebConsoleServer::start()` 先做 bind 预检，
  常见端口冲突会返回可读错误，竞态窗口内仍可能带日志退出。
- `--web-bind` 目前仅支持 IPv4。
- 远程访问需要反向代理 + TLS（v1 不自带 TLS）；同时需要 `--web-allowed-origin`。

## 7. 测试

```bash
# 纯路由/资产逻辑
./build/debug/web_router_tests

# WS 握手 + Origin 策略
./build/debug/ws_origin_tests

# 前端纯逻辑（语句切分 / 格式化 / 图抽取）
cd web && npm test

# 真实 server：HTTP 资产/gzip/304/HEAD/404/405/穿越 + WS Origin
./scripts/run-web-console-tests.sh \
    ./build/debug/eugraph-server "$(command -v pytest)" tests/web/test_web_console.py

# 现有官方 JS 驱动 WS 回归（无 Origin 路径）
EUGRAPH_BOLT_PORT=17696 \
EUGRAPH_NODE_DRIVER_PATH=$PWD/build/driver_node_modules/neo4j-6.2.0 \
node --test tests/bolt/test_js_ws_driver.cjs
```

## 8. 依赖与许可

- C++：Drogon 1.9.11（MIT）→ trantor 1.5.24（BSD-2）、jsoncpp（MIT）、brotli（MIT）、
  c-ares/openssl（传递依赖，均为宽松许可）。
- 前端：`neo4j-driver` 6.2.0（Apache-2.0）、Sigma 4.0.4 / graphology（MIT）、
  Vite/TypeScript/Vitest（开发期，MIT）。CodeMirror 自动补全为后续里程碑。
- 第三方声明随资产提供：`/licenses.txt`。
