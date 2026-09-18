#!/usr/bin/env python3
"""issues-to-ledger.py — regenerate a counted ledger FROM the GitHub issues.

DIRECTION, since 2026-09-17 (Victor): **the issues are the truth.** The ledger
files stay in the tree because they are BUILD INPUTS — `tests/logos/CMakeLists.txt`
reads them at configure time to generate per-row ctest tests, and six gates and
scripts read them offline — but nobody edits them by hand any more. They are
generated, committed artefacts, refreshed when we pick up new work. Think of them
as a lockfile: authored elsewhere, checked in, verified mechanically.

  issues-to-ledger.py --list backlog --check    # drift check: file vs issues, rc 1 if they differ
  issues-to-ledger.py --list backlog --write    # regenerate the file

An OPEN issue is a row. A CLOSED issue is a row that is gone — that is how closing
works now, and it is why nothing here ever deletes a row on its own initiative.

SAFETY RAILS, because a broken channel must never read as a verdict. Every one of
these has bitten this project in some other instrument:
  * a list that comes back EMPTY for a file that currently has rows is refused;
  * a pull that would remove more than a quarter of the rows is refused without --force;
  * the file's HEADER PROSE is preserved verbatim — it is not derived from issues
    and must not be lost;
  * `# TOTAL` is recomputed by DIRECT LISTING of what was written, never carried;
  * the write is atomic (temp file + rename), so an interrupted run cannot leave
    a half-written build input behind.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

REPO = "victor-smirnov/logos"
LEDGER_FILE = {
    "backlog": "tests/logos/unrowed_backlog.ledger",
    "squeue": "tests/logos/soundness_queue.ledger",
    "bc-admits": "tests/logos/bc_admits.ledger",
}
GENERATED_NOTE = (
    "# ⚠ GENERATED FROM THE GITHUB ISSUES — DO NOT EDIT BY HAND.\n"
    "# The issues are the source of truth (2026-09-17). Regenerate with\n"
    "#   scripts/issues-to-ledger.py --list {which} --write\n"
    "# and check for drift with `--check`. A hand edit here is lost at the next pull.\n"
)


def run(args: list[str]) -> tuple[int, str, str]:
    """Capture, then read rc. Never `cmd | head; echo $?` — that reports head's status."""
    p = subprocess.run(args, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def issue_rows(which: str, from_json: str | None = None) -> list[dict]:
    """Open issues carrying our marker for this list, oldest issue first.

    `from_json` reads a saved `gh issue list --json …` payload instead of calling
    the API. It exists so the SAFETY RAILS below can be exercised against an empty
    or truncated channel without touching real issues — an untested rail guarding a
    build input is no better than no rail."""
    if from_json:
        with open(from_json) as fh:
            out = fh.read()
    else:
        rc, out, err = run(["gh", "issue", "list", "--repo", REPO, "--state", "open",
                            "--limit", "1000", "--json", "number,title,body"])
        if rc != 0:
            sys.exit(f"gh issue list failed (rc {rc}): {err.strip()}")
    rows = []
    for it in sorted(json.loads(out), key=lambda i: i["number"]):
        body = it.get("body") or ""
        marker = next((l for l in body.splitlines() if l.startswith("<!-- ledger-sync:")), None)
        if not marker or f"list={which}" not in marker:
            continue
        rid = marker.split("id=", 1)[1].split()[0]
        blocks = body.split("```")
        if len(blocks) < 2:
            sys.exit(f"issue #{it['number']} ({rid}) has no verbatim block — refusing to guess")
        lines = [l for l in blocks[1].splitlines() if l.strip()]
        rows.append({"id": rid, "number": it["number"], "lines": lines})
    return rows


def split_file(path: str) -> tuple[list[str], list[str]]:
    """(header prose, everything else). The header is every line up to the first
    line that is neither blank nor a column-0 comment — i.e. up to the first row."""
    with open(path) as fh:
        all_lines = fh.read().splitlines()
    for i, ln in enumerate(all_lines):
        if ln.strip() and not ln.startswith("#"):
            return all_lines[:i], all_lines[i:]
    return all_lines, []


def current_ids(body: list[str]) -> list[str]:
    ids = []
    for ln in body:
        head = ln.split("#", 1)[0].split()
        if head and not ln.startswith((" ", "\t")):
            ids.append(head[0])
    return ids


def render(which: str, header: list[str], rows: list[dict], old_order: list[str]) -> str:
    """Keep the existing row order for rows that survive; append new ones by issue
    number. A stable order keeps the diff to what actually changed."""
    by_id = {r["id"]: r for r in rows}
    order = [i for i in old_order if i in by_id] + [r["id"] for r in rows if r["id"] not in old_order]
    out = list(header)
    if not any("GENERATED FROM THE GITHUB ISSUES" in l for l in header):
        out = [l for l in GENERATED_NOTE.format(which=which).splitlines()] + [""] + out
    for rid in order:
        out.extend(by_id[rid]["lines"])
        out.append("")
    while out and not out[-1].strip():
        out.pop()
    out += ["", f"# TOTAL {len(order)}", ""]
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", dest="which", required=True, choices=sorted(LEDGER_FILE))
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--force", action="store_true", help="allow a large shrink")
    ap.add_argument("--from-json", dest="from_json", metavar="PATH",
                    help="read a saved `gh issue list --json` payload instead of the API "
                         "(for exercising the safety rails offline)")
    a = ap.parse_args()

    path = LEDGER_FILE[a.which]
    header, body = split_file(path)
    old = current_ids(body)
    rows = issue_rows(a.which, a.from_json)

    if not rows and old:
        sys.exit(f"REFUSED: the issue list came back EMPTY while {path} holds {len(old)} rows. "
                 "An empty channel is not an empty ledger.")
    lost = [i for i in old if i not in {r['id'] for r in rows}]
    if old and len(lost) > max(1, len(old) // 4) and not a.force:
        sys.exit(f"REFUSED: this pull would remove {len(lost)} of {len(old)} rows "
                 f"({', '.join(lost[:5])}…). Pass --force if that is really intended.")

    text = render(a.which, header, rows, old)
    with open(path) as fh:
        before = fh.read()

    if a.check:
        if text == before:
            print(f"{path}: in sync with the issues ({len(rows)} rows)")
            return 0
        print(f"{path}: DRIFTED from the issues — regenerate with --write", file=sys.stderr)
        print(f"  file rows {len(old)}, issues {len(rows)}; removed {len(lost)}", file=sys.stderr)
        return 1

    if not a.write:
        print(f"{a.which}: {len(rows)} open issues -> {len(rows)} rows "
              f"(file has {len(old)}); {'IDENTICAL' if text == before else 'would change'}. "
              "Pass --write to regenerate or --check to gate on it.")
        return 0

    d = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".ledger-", suffix=".tmp")
    with os.fdopen(fd, "w") as fh:
        fh.write(text)
    os.replace(tmp, path)
    print(f"{path}: wrote {len(rows)} rows (was {len(old)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
