#!/usr/bin/env bash
# 分层依赖检查：禁止反向 include。
#
# 规则来源：docs/architecture/module-layout.md §1「分层与依赖方向」。
# 违规示例：storage 里 include query/ 的头文件 —— 这正是 issue #280 的成因
# （为了绕开反向依赖，把代码"下沉"到 storage/data/）。
#
# 用法：
#   ./scripts/check-layering.sh        # 检查，有违规退出码 1
#   ./scripts/check-layering.sh -v     # 额外打印每条规则与扫描到的文件数
#
# 依赖：bash + grep；无网络、无构建，可在 CI 的 lint job 里跑（<1s）。

set -uo pipefail

cd "$(dirname "$0")/.." || exit 2

VERBOSE=0
[[ "${1:-}" == "-v" ]] && VERBOSE=1

# 单个顶层模块允许依赖的顶层模块（空格分隔）。
# 依赖只能指向"更低/同一"层，见 docs/architecture/module-layout.md。
allowed_for() {
    case "$1" in
        common)  echo "common" ;;
        storage) echo "common storage" ;;
        query)   echo "common query storage" ;;
        graph)   echo "common graph query storage" ;;
        service) echo "common graph query service storage" ;;
        program) echo "common graph program service" ;;
        *)       echo "" ;;
    esac
}

# 生成代码不参与检查（ANTLR 产物、fbthrift gen-cpp2）。
is_generated() {
    case "$1" in
        src/query/parser/generated/*) return 0 ;;
        src/service/thrift/gen-cpp2/*) return 0 ;;
        *) return 1 ;;
    esac
}

# 例外白名单：已知的历史偏差 + 修复它的 issue。一行一个 `文件:被依赖模块`。
# 只允许加"已登记待修"的项；修完后必须从这里删掉（脚本会提示过期条目）。
ALLOWLIST=$(cat <<'EOF'
EOF
)

violations=0
stale_allow=0
scanned=0

report() {
    local file="$1" line="$2" from="$3" to="$4" snippet="$5"
    printf '%s:%s  [%s ↛ %s]  %s\n' "$file" "$line" "$from" "$to" "$snippet"
    violations=$((violations + 1))
}

allowlisted() {
    local key="$1:$2"
    while IFS= read -r entry; do
        [[ -z "$entry" ]] && continue
        [[ "$entry" == "$key" ]] && return 0
    done <<<"$ALLOWLIST"
    return 1
}

# ── 1. src/ 内部模块间 include ────────────────────────────────────────────
mapfile -t includes < <(
    grep -rInE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](common|storage|query|service|program|graph)/' \
        src --include=*.hpp --include=*.h --include=*.cpp 2>/dev/null
)

for entry in "${includes[@]}"; do
    file="${entry%%:*}"
    rest="${entry#*:}"
    lineno="${rest%%:*}"
    snippet="${rest#*:}"

    is_generated "$file" && continue

    from="${file#src/}"
    from="${from%%/*}"
    to=$(printf '%s\n' "$snippet" | sed -E 's/.*include[[:space:]]*[<"]([a-z]+)\/.*/\1/')

    [[ -z "$from" || -z "$to" ]] && continue
    scanned=$((scanned + 1))

    if [[ " $(allowed_for "$from") " == *" $to "* ]]; then
        [[ $VERBOSE -eq 1 ]] && printf 'ok  %s ↦ %s  (%s)\n' "$from" "$to" "$file"
        continue
    fi
    if allowlisted "$file" "$to"; then
        continue
    fi
    report "$file" "$lineno" "$from" "$to" "$(printf '%s' "$snippet" | sed -E 's/^[[:space:]]+//')"
done

# 白名单过期条目：登记的违规已经不存在了 ⇒ 提醒删掉，避免白名单腐烂。
while IFS= read -r entry; do
    [[ -z "$entry" ]] && continue
    f="${entry%%:*}"
    mod="${entry##*:}"
    if [[ ! -f "$f" ]] || ! grep -qE "#include[[:space:]]*[<\"]${mod}/" "$f" 2>/dev/null; then
        printf 'warn: 白名单条目已失效（文件不存在或已无该依赖），请删除：%s\n' "$entry"
        stale_allow=$((stale_allow + 1))
    fi
done <<<"$ALLOWLIST"

# ── 2. 第三方边界：WiredTiger 只能出现在 storage/ ────────────────────────
# 其他层只允许看到 IAsyncGraphDataStore 等接口，不能自己开 WT 会话/游标。
wt_leaks=$(grep -rInE '^[[:space:]]*#[[:space:]]*include[[:space:]]*<wiredtiger' src \
    --include=*.hpp --include=*.h --include=*.cpp 2>/dev/null | grep -v '^src/storage/' || true)
if [[ -n "$wt_leaks" ]]; then
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        printf '%s  [wiredtiger 必须留在 src/storage/]\n' "$line"
        violations=$((violations + 1))
    done <<<"$wt_leaks"
fi

# ── 3. 汇总 ──────────────────────────────────────────────────────────────
if [[ $VERBOSE -eq 1 ]]; then
    printf '\n扫描 %d 条跨模块 include（排除生成代码）\n' "$scanned"
fi

if [[ $violations -gt 0 ]]; then
    printf '\n✗ %d 处分层违规。规则见 docs/architecture/module-layout.md §1。\n' "$violations"
    printf '  修法：把代码放回它所属的层，而不是在低层加反向 include；\n'
    printf '  确需临时例外，请在 scripts/check-layering.sh 的 ALLOWLIST 登记（带 issue 号）。\n'
    exit 1
fi

printf '✓ 分层检查通过（%d 条跨模块 include，0 违规）\n' "$scanned"
exit 0
