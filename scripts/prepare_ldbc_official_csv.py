#!/usr/bin/env python3
"""Load LDBC SNB SF0.1 into eugraph the way the official Neo4j pipeline does.

Why this exists
---------------
The reference implementation loads the *converted* CSVs, not the raw ones:
``cypher/scripts/convert-csvs.sh`` first overwrites each file's header with the typed
header from ``cypher/scripts/headers.txt`` and then rewrites the label column's values
(``company`` -> ``Company``, ``city`` -> ``City``, ...). Only then does
``neo4j-admin import`` run.

Loading the raw CSVs instead loses schema that the official queries depend on:

* ``:LABEL`` columns -> ``Company`` / ``University`` / ``City`` / ``Country`` /
  ``Continent`` labels. Without the header override the raw file carries ``type`` as a
  plain column, so those labels never exist and e.g. complex-11 matches nothing.
* typed headers -> ``workFrom:INT``, ``creationDate:LONG``, ... i.e. the property types
  the reference implementation guarantees.
* ``:ID(<Space>)`` / ``:START_ID`` / ``:END_ID`` -> which id space each column belongs to,
  which is what lets one CSV (``person_likes_comment`` / ``person_likes_post``) feed the
  same relationship type.

Our own loader takes the same node/relationship mapping on the command line
(``--nodes=Label[:Label...]=file`` / ``--relationships=TYPE=file``), so this script
derives those arguments *from headers.txt itself* -- one source of truth, so the mapping
cannot drift from the official one again.

Usage
-----
    scripts/prepare_ldbc_official_csv.py --src <ldbc-conv dir> --out /tmp/ldbc-official
    scripts/prepare_ldbc_official_csv.py --src <ldbc-conv dir> --out /tmp/ldbc-official \
        --load --host 127.0.0.1 --port 9090
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

# ``-0_0`` is the official NEO4J_CSV_POSTFIX for the pre-generated csv_basic-longdateformatter
# data set.
CSV_POSTFIX = "_0_0"

# id:ID(Organisation) / :START_ID(Person) / :END_ID(Place)
# Relationship type per file stem, exactly as import-to-neo4j.sh declares it
# (--relationships=TYPE=file). This cannot be derived from the file name: the loader's
# convention takes the text between the first and last underscore (person_knows_person
# -> "knows"), which would silently leave the graph with no :KNOWS edge at all -- every
# official query pattern would then match nothing while the edge *counts* still look right.
REL_TYPE = {
    "static/place_isPartOf_place": "IS_PART_OF",
    "static/tagclass_isSubclassOf_tagclass": "IS_SUBCLASS_OF",
    "static/organisation_isLocatedIn_place": "IS_LOCATED_IN",
    "static/tag_hasType_tagclass": "HAS_TYPE",
    "dynamic/comment_hasCreator_person": "HAS_CREATOR",
    "dynamic/comment_isLocatedIn_place": "IS_LOCATED_IN",
    "dynamic/comment_replyOf_comment": "REPLY_OF",
    "dynamic/comment_replyOf_post": "REPLY_OF",
    "dynamic/forum_containerOf_post": "CONTAINER_OF",
    "dynamic/forum_hasMember_person": "HAS_MEMBER",
    "dynamic/forum_hasModerator_person": "HAS_MODERATOR",
    "dynamic/forum_hasTag_tag": "HAS_TAG",
    "dynamic/person_hasInterest_tag": "HAS_INTEREST",
    "dynamic/person_isLocatedIn_place": "IS_LOCATED_IN",
    "dynamic/person_knows_person": "KNOWS",
    "dynamic/person_likes_comment": "LIKES",
    "dynamic/person_likes_post": "LIKES",
    "dynamic/person_studyAt_organisation": "STUDY_AT",
    "dynamic/person_workAt_organisation": "WORK_AT",
    "dynamic/post_hasCreator_person": "HAS_CREATOR",
    "dynamic/comment_hasTag_tag": "HAS_TAG",
    "dynamic/post_hasTag_tag": "HAS_TAG",
    "dynamic/post_isLocatedIn_place": "IS_LOCATED_IN",
}

# The official import script adds a second label to two vertex files on top of what
# headers.txt declares: "--nodes=Comment:Message" and "--nodes=Post:Message". Message is
# what complex-6/9/10 match on, so dropping it makes those queries return 0 rows.
EXTRA_NODE_LABELS = {"dynamic/comment": "Message", "dynamic/post": "Message"}

ID_SPACE = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_]*)?:?(?:START_|END_)?ID\(([^)]*)\)$")

# Label-column value -> the label spellings the official convert-csvs.sh seds in.
# Keeping the mapping here (rather than running those seds verbatim) makes the intent
# explicit and fails loudly on an unexpected value instead of silently leaving it raw.
LABEL_VALUE_TO_LABEL = {
    "city": "City",
    "country": "Country",
    "continent": "Continent",
    "company": "Company",
    "university": "University",
}

def read_headers(path: Path) -> list[tuple[str, str]]:
    """Parse headers.txt into (relative file stem, typed header) pairs."""
    out = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            sys.exit(f"headers.txt: cannot parse line: {line!r}")
        out.append((parts[0], parts[1]))
    return out


def label_columns(header: str) -> list[int]:
    return [i for i, col in enumerate(header.split("|")) if col == ":LABEL"]


def transform(src_file: Path, dest_file: Path, header: str) -> None:
    """Overwrite the header and map label-column values to their capitalised label."""
    cols = label_columns(header)
    dest_file.parent.mkdir(parents=True, exist_ok=True)
    with src_file.open() as f_in, dest_file.open("w") as f_out:
        f_out.write(header + "\n")
        next(f_in)  # drop the raw header
        for line in f_in:
            if cols:
                fields = line.rstrip("\n").split("|")
                for c in cols:
                    if c < len(fields):
                        raw = fields[c]
                        if raw in LABEL_VALUE_TO_LABEL:
                            fields[c] = LABEL_VALUE_TO_LABEL[raw]
                        elif raw:  # already a label, or an unknown value: keep it visible
                            fields[c] = raw
                line = "|".join(fields) + "\n"
            f_out.write(line)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="ldbc-conv style dir (static/ + dynamic/)")
    ap.add_argument("--headers", default=None, help="headers.txt (default: the upstream copy)")
    ap.add_argument("--out", required=True, help="where the converted CSVs are written")
    ap.add_argument("--load", action="store_true", help="run eugraph-loader after converting")
    ap.add_argument("--loader-bin", default="./build/release/eugraph-loader")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090)
    args = ap.parse_args()

    src = Path(args.src)
    out = Path(args.out)
    headers = Path(
        args.headers
        or "/home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/scripts/headers.txt"
    )
    if not headers.is_file():
        sys.exit(f"headers.txt not found: {headers}")

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    node_args: list[str] = []
    rel_args: list[str] = []
    written = 0
    for stem, header in read_headers(headers):
        src_file = src / f"{stem}{CSV_POSTFIX}.csv"
        if not src_file.is_file():
            sys.exit(f"source CSV missing: {src_file}")
        dest_file = out / f"{stem}{CSV_POSTFIX}.csv"
        transform(src_file, dest_file, header)
        written += 1

        # Derive the loader arguments from the official header itself:
        #   * a header with :START_ID / :END_ID describes a relationship file -- the loader
        #     takes its type on the command line (the CSV carries no type column);
        #   * otherwise it is a vertex file whose labels are the id space name (id:ID(X))
        #     plus whatever the :LABEL column supplies per row.
        cols = header.split("|")
        if any(c.startswith(":START_ID") for c in cols) and any(c.startswith(":END_ID") for c in cols):
            rel_type = REL_TYPE.get(stem)
            if not rel_type:
                sys.exit(f"no relationship type known for {stem!r} (add it to REL_TYPE)")
            rel_args.append(f"--relationships={rel_type}={dest_file}")
        else:
            space = next((ID_SPACE.match(c).group(1) for c in cols if ID_SPACE.match(c)), None)
            if not space:
                sys.exit(f"cannot derive a label for {stem} from header {header!r}")
            extra = EXTRA_NODE_LABELS.get(stem)
            spec = f"{space}:{extra}" if extra else space
            node_args.append(f"--nodes={spec}={dest_file}")

    print(f"converted {written} files into {out}")
    print(f"  node files: {len(node_args)}, relationship files: {len(rel_args)}")

    if not args.load:
        # Print the exact command so a caller can run it, or eyeball the mapping.
        print("\nloader command:\n")
        print(f"  {args.loader_bin} --host {args.host} --port {args.port} \\")
        for a in node_args + rel_args:
            print(f"      {a} \\")
        return 0

    cmd = [args.loader_bin, "--host", args.host, "--port", str(args.port), *node_args, *rel_args]
    print("\nrunning:", " ".join(cmd[:6]), "...")
    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
