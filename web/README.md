# EuGraph Web Console (frontend)

This directory is the source of the single-page console embedded in
`eugraph-server`. The C++ build **never** runs npm: it consumes the committed
`web/dist` directory.

## Requirements

- Node.js >= 22 (`.nvmrc` pins the CI version)
- npm (lockfile: `package-lock.json`; use `npm ci`)

## Development loop

```bash
# terminal 1: backend (Bolt + /api/info)
eugraph-server --data-dir ./eugraph-data --bolt-port 7687

# terminal 2: Vite dev server with HMR
cd web
npm ci
npm run dev            # http://127.0.0.1:5173
```

`/api` is proxied to `http://127.0.0.1:7475`; the page connects to Bolt directly
on port 7687. No C++ rebuild is needed while iterating on TS/CSS.

## Integrating into the binary

```bash
./scripts/build-web.sh          # npm ci (when needed) + npm run build
cmake --build --preset debug    # regenerates web_assets.generated.cpp and links
```

Only the generated asset translation unit is recompiled. `web/dist` is committed
so C++-only contributors and CI do not need Node.

## Checks

```bash
npm test                        # vitest
./scripts/build-web.sh --check  # CI: fail if web/dist is stale
./scripts/build-web.sh --check-size
```

See [../docs/service/web-console.md](../docs/service/web-console.md) for the
design, packaging pipeline, security model and limits.
