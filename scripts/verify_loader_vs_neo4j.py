"""独立核对：eugraph 导入结果 vs neo4j-admin import 定义。

用法：
    # 1) 起一个空库并装载（示例端口 19100 / bolt 17688）
    ./build/debug/eugraph-server -d /tmp/eugraph_check --thrift-port 19100 --bolt-port 17688 &
    ./build/debug/eugraph-loader --schema \
        social_network-sf0.1-CsvComposite-LongDateFormatter/loader-schema.json \
        --data-dir social_network-sf0.1-CsvComposite-LongDateFormatter \
        --host 127.0.0.1 --port 19100 --batch-size 2000 --rpc-connections 2 --parallel-files 2
    # 2) 核对（退出码非 0 = 有不一致）
    python3 scripts/verify_loader_vs_neo4j.py

环境变量：EUGRA_BOLT（默认 bolt://127.0.0.1:17688）、EUGRA_DB（默认 default）。

判据来源（外部实现，不是本项目推断）：
  * 关系类型名        : cypher/scripts/import-to-neo4j.sh 的 --relationships=TYPE="path"
  * 节点标签名        : 同文件的 --nodes=Label="path"（含 `Comment:Message` 这类追加标签）
  * 标签大小写改写    : cypher/scripts/convert-csvs.sh 的 sed 规则
  * 期望条数          : 语料 CSV 自身的行数（表头除外）
  * 列名与列类型      : cypher/scripts/headers.txt

注意：neo4j 的 import 不产生 place/organisation.type 的**派生细分标签**
（City/Country/Continent/Company/University）。本核对把这类差异单列，
不混进"一致性"结论里。
"""
import os
import re
import sys

from neo4j import GraphDatabase

SCRIPTS = "/mnt/f/code/ldbc_snb_interactive_v1_impls/cypher/scripts"
CORPUS = "/mnt/f/code/eugraph/social_network-sf0.1-CsvComposite-LongDateFormatter"
IMPORT = os.path.join(SCRIPTS, "import-to-neo4j.sh")
CONVERT = os.path.join(SCRIPTS, "convert-csvs.sh")

# ---------- 1) 从 neo4j 脚本抽取期望 ----------

src = open(IMPORT, encoding="utf-8").read()

# --nodes=Label="path"  以及 --nodes=Comment:Message="path"
node_specs = re.findall(r'--nodes=([A-Za-z][\w:]*)=', src)
label_of_file = {}   # 语料相对路径 -> [标签...]
for spec in node_specs:
    parts = spec.split(":")
    primary, extra = parts[0], parts[1:]
    # 该行的路径紧随其后
    m = re.search(r'--nodes=' + re.escape(spec) + r'="([^"]+)"', src)
    raw = m.group(1)
    # /import/static/place${NEO4J_CSV_POSTFIX} -> static/place
    rel = raw.replace("/import/", "")
    rel = re.sub(r"\$\{NEO4J_CSV_POSTFIX\}", "", rel)
    label_of_file.setdefault(rel, [])
    label_of_file[rel] = [primary] + extra

# 收集该次 import 涉及的全部文件
node_files = set()
for spec in node_specs:
    m = re.search(r'--nodes=' + re.escape(spec) + r'="([^"]+)"', src)
    raw = re.sub(r"\$\{NEO4J_CSV_POSTFIX\}", "", m.group(1).replace("/import/", ""))
    node_files.add(raw)

rel_type_of_file = {}   # 语料相对路径 -> 关系类型
rel_files = set()
for spec in re.findall(r'--relationships=([A-Z_]+)="([^"]+)"', src):
    rtype, raw = spec
    rel = re.sub(r"\$\{NEO4J_CSV_POSTFIX\}", "", raw.replace("/import/", ""))
    rel_type_of_file[rel] = rtype
    rel_files.add(rel)

# convert-csvs.sh 的 sed 改写：s/|city$/|City/ ... 作用在 static/place 或 static/organisation
case_rules = {}   # "static/place" -> {"city": "City", ...}
for line in open(CONVERT, encoding="utf-8"):
    m = re.search(r"sed -i\.bkp \"s/\|(\w+)\|?\$?/\|(\w+)\|?/\" \"\$\{NEO4J_CONVERTED_CSV_DIR\}/(static/\w+)", line)
    if not m:
        continue
    low, up, rel = m.group(1), m.group(2), m.group(3)
    case_rules.setdefault(rel, {})[low] = up


def corpus_path(rel):
    """static/place -> static/place_0_0.csv"""
    directory = os.path.dirname(rel)
    base = os.path.basename(rel)
    cand = os.path.join(CORPUS, directory, base + "_0_0.csv")
    if os.path.exists(cand):
        return cand
    raise FileNotFoundError(cand)


def rows(rel):
    with open(corpus_path(rel), encoding="utf-8") as f:
        return sum(1 for _ in f) - 1


def header_of(rel):
    with open(corpus_path(rel), encoding="utf-8") as f:
        return f.readline().rstrip("\n").split("|")


# ---------- 2) 期望的标签/关系计数 ----------

# 顶点标签：neo4j 每条 --nodes 一个主标签；本语料里 place/organisation 的行级标签
# 由 convert-csvs.sh 的 sed 改写而来（neo4j 也只把它们当额外标签，不是独立 --nodes）
expect_label = {}
for rel in sorted(node_files):
    for lab in label_of_file.get(rel, []):
        expect_label[lab] = expect_label.get(lab, 0) + rows(rel)

# 细分标签：headers.txt 把 static/place 的 `type` 列改写成 `:LABEL`，convert-csvs.sh 的 sed
# 再把取值首字母大写 —— 因此 neo4j 的 Place 顶点同样带 City/Country/Continent 标签。
# 判据来源与 --nodes 同级（都是 neo4j 脚本），故同样纳入严格核对。
derived = {}
for rel, rules in case_rules.items():
    col_idx = header_of(rel).index("type")
    counts = {}
    with open(corpus_path(rel), encoding="utf-8") as f:
        f.readline()
        for line in f:
            v = line.rstrip("\n").split("|")[col_idx]
            v = rules.get(v, v)
            counts[v] = counts.get(v, 0) + 1
    for v, c in counts.items():
        derived[v] = derived.get(v, 0) + c

expect_rel = {}
for rel in sorted(rel_files):
    expect_rel[rel_type_of_file[rel]] = expect_rel.get(rel_type_of_file[rel], 0) + rows(rel)

# ---------- 3) 实测 ----------

driver = GraphDatabase.driver(os.environ.get("EUGRA_BOLT", "bolt://127.0.0.1:17688"), auth=None)
DB = os.environ.get("EUGRA_DB", "default")


def count(cypher):
    with driver.session(database=DB) as s:
        return s.run(cypher).single()[0]


ok = bad = 0


def check(name, expected, actual, strict=True):
    global ok, bad
    if expected == actual:
        ok += 1
        print(f"  ✅ {name:22s} neo4j 口径 {expected:>10,}   eugraph {actual:>10,}")
    else:
        bad += 1
        print(f"  ❌ {name:22s} neo4j 口径 {expected:>10,}   eugraph {actual:>10,}")


print("=== A) 顶点标签（neo4j --nodes 口径）===")
for lab in sorted(expect_label):
    check(lab, expect_label[lab], count(f"MATCH (n:{lab}) RETURN count(n)"))

print("\n=== B) 行级细分标签（headers.txt 的 :LABEL + convert-csvs.sh 的 sed）===")
for lab in sorted(derived):
    check(lab, derived[lab], count(f"MATCH (n:{lab}) RETURN count(n)"))

print("\n=== C) 关系类型（neo4j --relationships 口径）===")
for rtype in sorted(expect_rel):
    check(rtype, expect_rel[rtype], count(f"MATCH ()-[r:{rtype}]->() RETURN count(r)"))

print("\n=== D) 总量 ===")
# 顶点总量：neo4j 的多标签 --nodes=Comment:Message 会让同一批顶点同时计入两个标签，
# 直接累加会重复计数。总量应按【neo4j 的 --nodes 条目数】算（每条目一份文件）。
total_v = sum(rows(rel) for rel in sorted(node_files))
total_e = sum(expect_rel.values())
check("顶点总量", total_v, count("MATCH (n) RETURN count(n)"))
check("边总量", total_e, count("MATCH ()-[r]->() RETURN count(r)"))

driver.close()
print(f"\n  严格核对: {ok} 项一致, {bad} 项不一致")
sys.exit(1 if bad else 0)
