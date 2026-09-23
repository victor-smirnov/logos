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
import hashlib
import json
import os
import re
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


def rows_digest(row_lines: list[str]) -> str:
    """A digest over the ROW REGION only, so the gate can catch a hand edit OFFLINE.

    The drift check against the issues needs the network and therefore cannot be a
    build gate: `cmake` and every gate must work without a token, and an API outage
    must never read as a red test. This digest is the offline half — it does not know
    what the issues say, but it knows whether anyone edited the file after the
    generator wrote it, which is exactly the "these files are read-only" rule."""
    return hashlib.sha256("\n".join(row_lines).encode()).hexdigest()[:32]


def inter_row_blocks(body: list[str]) -> tuple[dict[str, list[str]], list[str]]:
    """Column-0 `#` blocks standing BETWEEN rows, keyed by the row they precede.

    ⚠ THESE BELONG TO THE FILE, NOT TO ANY ISSUE, AND REGENERATION MUST CARRY THEM
    FORWARD. They are the arc's record of why rows left — dated blocks like
    "FOUR OF THE SIX CLOSED THE SAME DAY THEY RETURNED … 97 -> 93", written by the
    round that closed them. Nothing on GitHub holds them.

    Measured the hard way: an earlier `render` rebuilt the file as header + rows +
    tail and silently dropped every one of them — `bc_admits` 740 inter-row lines
    to 2, `soundness_queue` 142 to 2. It went unnoticed through two verification
    passes because those passes compared ROW LINES ONLY ("60 of 60, zero differ"),
    a property chosen so that it could not see the loss. The backlog survived only
    because its prose is INDENTED and therefore attaches to its row.

    A row's own indented prose is NOT collected here — it travels with the row.
    """
    blocks: dict[str, list[str]] = {}
    pending: list[str] = []
    for l in body:
        if l.strip() and not l.startswith(("#", " ", "\t")):
            rid = l.split()[0]
            while pending and not pending[-1].strip():
                pending.pop()
            if pending:
                blocks[rid] = pending
            pending = []
        elif l.startswith(("# TOTAL", "# SYNC-HASH")):
            # ⚠ THE GENERATOR'S OWN TAIL IS NOT THE FILE'S RECORD. These two lines sit
            # at column 0 after the last row, so they look exactly like trailing
            # commentary — and collecting them made every write ABSORB the previous
            # run's tail into the row region and then append a fresh one. The region
            # grew by two lines per write, the digest covered a stale tail, and
            # `--verify` (which strips them) could never agree. Measured as three
            # different digests for one file: recorded 56f34a13, verify-side 5a67ee10,
            # render-side 9cda729d.
            continue
        elif l.startswith("#") or (pending and not l.strip()):
            pending.append(l)
    while pending and not pending[-1].strip():
        pending.pop()
    return blocks, pending


def render(which: str, header: list[str], rows: list[dict], old_order: list[str],
           blocks: dict[str, list[str]] | None = None,
           trailing: list[str] | None = None) -> str:
    """Keep the existing row order for rows that survive; append new ones by issue
    number. A stable order keeps the diff to what actually changed.

    `blocks`/`trailing` carry the file's own inter-row commentary through the round
    trip; a block whose row is gone is kept in place (attached to the next surviving
    row) rather than deleted, because it usually records why an EARLIER row left."""
    by_id = {r["id"]: r for r in rows}
    order = [i for i in old_order if i in by_id] + [r["id"] for r in rows if r["id"] not in old_order]
    blocks = blocks or {}
    head = list(header)
    if not any("GENERATED FROM THE GITHUB ISSUES" in l for l in header):
        head = [l for l in GENERATED_NOTE.format(which=which).splitlines()] + [""] + head
    orphaned: list[str] = []          # blocks whose row no longer exists
    for rid, blk in blocks.items():
        if rid not in by_id:
            orphaned.extend(blk)
    row_lines: list[str] = []
    for rid in order:
        blk = blocks.get(rid)
        if blk:
            row_lines.extend(blk)
        row_lines.extend(by_id[rid]["lines"])
        row_lines.append("")
    if orphaned:
        row_lines.extend(orphaned)
        row_lines.append("")
    if trailing:
        row_lines.extend(trailing)
        row_lines.append("")
    while row_lines and not row_lines[-1].strip():
        row_lines.pop()
    tail = ["", f"# TOTAL {len(order)}", f"# SYNC-HASH {rows_digest(row_lines)}", ""]
    return "\n".join(head + row_lines + tail)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", dest="which", required=True, choices=sorted(LEDGER_FILE))
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--force", action="store_true", help="allow a large shrink")
    ap.add_argument("--from-json", dest="from_json", metavar="PATH",
                    help="read a saved `gh issue list --json` payload instead of the API "
                         "(for exercising the safety rails offline)")
    ap.add_argument("--verify", action="store_true",
                    help="OFFLINE: recompute the row digest and compare it with the file's "
                         "own `# SYNC-HASH`. Catches a hand edit without touching the network.")
    ap.add_argument("--file", dest="file_override", metavar="PATH",
                    help="verify THIS file instead of the list's own ledger. Exists so a "
                         "gate's canary can feed a deliberately corrupted copy through the "
                         "SAME checker: a canary that does not ride the real code path "
                         "reports 'caught' even when every real check crashed.")
    a = ap.parse_args()

    path = a.file_override or LEDGER_FILE[a.which]
    header, body = split_file(path)
    old = current_ids(body)

    if a.verify:
        # Deliberately BEFORE any API call: a build gate must work with no token and
        # no network, and an API outage must never read as a red test.
        recorded = next((l.split()[2] for l in body if l.startswith("# SYNC-HASH ")), None)
        rows_region = list(body)
        while rows_region and (not rows_region[-1].strip()
                               or rows_region[-1].startswith(("# TOTAL", "# SYNC-HASH"))):
            rows_region.pop()
        actual = rows_digest(rows_region)
        if recorded is None:
            print(f"{path}: no `# SYNC-HASH` line — regenerate with --write", file=sys.stderr)
            return 1
        if recorded != actual:
            print(f"{path}: HAND-EDITED — recorded {recorded}, actual {actual}. This file is "
                  "generated from the GitHub issues; edit the issue, then --write.",
                  file=sys.stderr)
            return 1
        print(f"{path}: digest OK ({len(old)} rows, {actual})")
        return 0

    rows = issue_rows(a.which, a.from_json)

    # ⚠ A RAIL TEST MUST NOT BE ABLE TO DAMAGE A BUILD INPUT. `--from-json` exists to
    # exercise the refusals against a faked channel; when I used it that way it wrote
    # the real ledger down to 17 rows, and the digest check could not see it because a
    # generated write recomputes the digest over the damage. Simulated input is now
    # read-only unless the operator says otherwise in as many words.
    if a.from_json and a.write and not a.force:
        sys.exit("REFUSED: --write with --from-json would rewrite a real ledger from a "
                 "SIMULATED channel. Drop --write to see what it would do, or add --force "
                 "if you genuinely mean to write from that payload.")

    if not rows and old and not a.force:
        sys.exit(f"REFUSED: the issue list came back EMPTY while {path} holds {len(old)} rows. "
                 "An empty channel is not an empty ledger. If the ledger IS now empty "
                 "(its last row closed), pass --force and say so in the commit.")
    lost = [i for i in old if i not in {r['id'] for r in rows}]
    if old and len(lost) > max(1, len(old) // 4) and not a.force:
        sys.exit(f"REFUSED: this pull would remove {len(lost)} of {len(old)} rows "
                 f"({', '.join(lost[:5])}…). Pass --force if that is really intended.")

    # ── THE CLOSURE GUARD ───────────────────────────────────────────────────────
    # Under "issues are the truth" the dangerous direction INVERTED. It used to be
    # conservative: a stale row kept a gate red for a defect already fixed. Now a
    # closed issue DELETES a row, so closing one by mistake silently stops a gate
    # watching a live defect — permissive drift, the kind a green corpus cannot see.
    # So a removal must be justified by evidence in the tree, not by the click alone.
    if lost and not a.force:
        unjustified = []
        for rid in lost:
            rc, out, _ = run(["git", "log", "--format=%H", "-1", f"--grep={rid}", "--fixed-strings"])
            if rc == 0 and out.strip():
                continue                      # a commit names the row — it was worked
            # ⚠ THE PATH FALLBACK IS LIST-SPECIFIC AND ASSUMING OTHERWISE MADE THIS
            # GUARD DEAD CODE. Field 2 is a program path for `squeue` and `bc-admits`,
            # but for `backlog` it is the CARRIERS COUNT ("1"), so `exists("1")` was
            # false and every removal took the "program is gone" branch. Measured: a
            # row nothing had touched was waved through and the file was written.
            if a.which != "backlog":
                prog = next((r for r in body if r.startswith(rid)), "")
                parts_ = prog.split("#", 1)[0].split()
                path_ = parts_[2] if len(parts_) > 2 else None
                if path_ and path_.startswith("tests/") and not (
                        os.path.exists(path_) or os.path.exists(path_ + ".logos")):
                    continue                  # the program is gone — the row went with it
            unjustified.append(rid)
        if unjustified:
            sys.exit(
                f"REFUSED: {len(unjustified)} row(s) would be removed with no landed evidence: "
                + ", ".join(unjustified[:5]) + ("…" if len(unjustified) > 5 else "")
                + "\nA closed issue deletes a counted row. Land the fix (a commit naming the id, "
                  "or the program removed), or pass --force if the row is being discarded on "
                  "purpose — and say so in the commit that carries this pull.")

    blocks, trailing = inter_row_blocks(body)
    text = render(a.which, header, rows, old, blocks, trailing)
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
