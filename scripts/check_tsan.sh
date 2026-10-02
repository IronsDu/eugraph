#!/bin/bash
# TSan 并发闸门：用 TSan 构建的服务端跑一段混合并发查询，任何**未被抑制**的 data race 都判红。
#
# 用法: scripts/check_tsan.sh [构建目录=build/tsan] [秒数=20] [线程数=8]
# 前置: 该构建目录需已存在（见 docs/storage/thread-bound-session-cursor-pool.md §12 的构建命令）。
# 说明: 抑制清单 tests/tsan/tsan.supp 只覆盖 folly/thrift 的已知噪声；storage/session/cursor
#       相关符号一律不抑制 —— 它们出现即视为失败。
set -u
BUILD_DIR=${1:-build/tsan}
SECS=${2:-20}
THREADS=${3:-8}
BIN="$BUILD_DIR/eugraph-server"
DATA=${EUGRAPH_TSAN_DATA:-/home/dodo/code/fuck/eugraph-sf0.1-fresh}
WORK=${EUGRAPH_TSAN_WORK:-/home/dodo/tckwork/tsan_gate}
REPORT_DIR="$WORK/reports"

if [ ! -x "$BIN" ]; then
    echo "缺少 TSan 构建: $BIN（请先按文档构建）" >&2
    exit 2
fi

for p in $(pgrep -x eugraph-server 2>/dev/null); do kill "$p" 2>/dev/null; done
sleep 2
rm -rf "$WORK"; mkdir -p "$REPORT_DIR"

TSAN_OPTIONS="halt_on_error=0:log_path=$REPORT_DIR/report:suppressions=$(pwd)/tests/tsan/tsan.supp" \
    nohup "$BIN" --thrift-port 9091 --bolt-port 7689 --data-dir "$DATA" \
    --compute-threads "$THREADS" --storage-io-threads 4 > "$WORK/server.log" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 20); do
    sleep 2
    ss -ltn 2>/dev/null | grep -q ":7689 " && break
done
if ! ss -ltn 2>/dev/null | grep -q ":7689 "; then
    echo "TSan 服务端启动失败:" >&2; tail -5 "$WORK/server.log" >&2; exit 2
fi

python3 - "$SECS" "$THREADS" <<'PY'
import sys, threading, time
from neo4j import GraphDatabase
secs, threads = int(sys.argv[1]), int(sys.argv[2])
QS = ["MATCH (m:Message) RETURN count(m) AS n",
      "MATCH (m:Message) RETURN count(m.creationDate)+count(m.length)+count(m.browserUsed) AS n",
      "MATCH (n) RETURN count(n) AS n",
      "MATCH ()-[r:KNOWS]->() RETURN count(r) AS n",
      "MATCH (r:Person {id:32985348834013})-[:KNOWS*1..2]-(f:Person) WHERE NOT f=r "
      "WITH collect(DISTINCT f) AS fs UNWIND fs AS f MATCH (f)<-[:HAS_CREATOR]-(m:Message) "
      "WHERE m.creationDate < 1346112000000 RETURN count(m) AS n"]
def worker(i):
    d = GraphDatabase.driver("bolt://127.0.0.1:7689", auth=("neo4j", "eugraph"), connection_timeout=300)
    end, k = time.monotonic() + secs, 0
    with d.session(database="default") as s:
        while time.monotonic() < end:
            s.run(QS[(i + k) % len(QS)]).consume(); k += 1
    d.close()
th = [threading.Thread(target=worker, args=(i,)) for i in range(threads)]
for t in th: t.start()
for t in th: t.join()
print(f"  {threads} 线程 x {secs}s 并发查询完成")
PY

ALIVE=1
pgrep -x eugraph-server > /dev/null || ALIVE=0
for p in $(pgrep -x eugraph-server 2>/dev/null); do kill "$p" 2>/dev/null; done

RACES=$(grep -h -c "WARNING: ThreadSanitizer" "$REPORT_DIR"/report* 2>/dev/null | paste -sd+ | bc 2>/dev/null)
RACES=${RACES:-0}
STORE_HITS=$(grep -h -cE "wt_store_base|sync_graph_data_store.*cursor|open_cursor" "$REPORT_DIR"/report* 2>/dev/null | paste -sd+ | bc 2>/dev/null)
STORE_HITS=${STORE_HITS:-0}

echo "  服务端存活: $([ "$ALIVE" = 1 ] && echo 是 || echo 否)"
echo "  未抑制的 TSan 报告: $RACES"
echo "  报告中涉及存储/session/cursor 的行: $STORE_HITS"

if [ "$ALIVE" != 1 ] || [ "$RACES" != 0 ] || [ "$STORE_HITS" != 0 ]; then
    echo "TSan 闸门: FAIL"; exit 1
fi
echo "TSan 闸门: PASS"
