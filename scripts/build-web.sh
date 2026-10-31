#!/usr/bin/env bash
#
# Build the embedded web console frontend.
#
#   ./scripts/build-web.sh              # install deps when needed, build web/dist
#   ./scripts/build-web.sh --check      # build to a temp dir and diff against web/dist (CI)
#   ./scripts/build-web.sh --check-size # print raw/gzip budgets
#
# Node/npm are only required when the frontend is rebuilt; the C++ build reads
# the committed web/dist and never invokes npm.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="${ROOT}/web"
MODE="build"
for arg in "$@"; do
    case "$arg" in
        --check) MODE="check" ;;
        --check-size) MODE="check-size" ;;
        -h|--help)
            sed -n '2,12p' "$0"
            exit 0
            ;;
        *)
            echo "unknown option: $arg" >&2
            exit 2
            ;;
    esac
done

if [[ ! -d "${WEB_DIR}" ]]; then
    echo "error: ${WEB_DIR} does not exist" >&2
    exit 1
fi
cd "${WEB_DIR}"

if ! command -v node >/dev/null 2>&1 || ! command -v npm >/dev/null 2>&1; then
    echo "error: node/npm are required to rebuild web/dist (Node >= 22)" >&2
    exit 1
fi

need_install() {
    [[ ! -d node_modules ]] && return 0
    [[ ! -f node_modules/.package-lock.json ]] && return 0
    [[ package-lock.json -nt node_modules/.package-lock.json ]] && return 0
    return 1
}

if need_install; then
    echo "[web] installing dependencies (npm ci)"
    npm ci --no-audit --no-fund
fi

size_report() {
    local dist="$1"
    local raw gz
    raw=$(du -sb "${dist}" | cut -f1)
    gz=$(find "${dist}" -type f -print0 | sort -z | xargs -0 cat | gzip -9 -c | wc -c)
    printf '[web] raw %s bytes, gzip %s bytes\n' "${raw}" "${gz}"
}

if [[ "${MODE}" == "check-size" ]]; then
    size_report dist
    exit 0
fi

if [[ "${MODE}" == "check" ]]; then
    tmp="$(mktemp -d -t eugraph-web-check-XXXXXX)"
    trap 'rm -rf "${tmp}"' EXIT
    echo "[web] building to ${tmp} for comparison"
    npm run build -- --outDir "${tmp}" --emptyOutDir
    if ! diff -r -q dist "${tmp}"; then
        echo "error: web/dist is out of date; run ./scripts/build-web.sh and commit web/dist" >&2
        exit 1
    fi
    echo "[web] web/dist is up to date"
    size_report dist
    exit 0
fi

echo "[web] building web/dist"
npm run build
size_report dist
