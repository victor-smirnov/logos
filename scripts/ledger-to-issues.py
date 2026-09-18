#!/usr/bin/env python3
"""ledger-to-issues.py — project the counted ledgers onto GitHub issues.

⚠ DIRECTION REVERSED 2026-09-17 (Victor): **the GitHub issues are the truth.** This
script is now the SEEDING half — it publishes rows that have no issue yet, so a list
can be moved onto the tracker once. After that, authoring happens in the issues and
`scripts/issues-to-ledger.py` regenerates the ledger file from them.

The ledger files stay in the tree because they are BUILD INPUTS — `tests/logos/
CMakeLists.txt` reads them at configure time to generate per-row ctest tests, and six
gates and scripts read them offline — but they are generated artefacts now, like a
lockfile. Nothing here writes to a ledger; this script only reads one to seed issues
from it. Use it on a list that is not yet on the tracker; do not use it to "push back"
an edit made in a file, because the next pull would overwrite that edit anyway.

Rows come from `arc-progress.py --rows`, which parses each file the way its gate
does and REFUSES to emit when its row count disagrees with its own totals. This
script does not re-parse the ledgers: a second parser is a second notion of one
concept, and the narrow one wins silently.

  ledger-to-issues.py --list backlog            # dry run: say what would happen
  ledger-to-issues.py --list backlog --apply    # create/update for real

Idempotent: each issue carries a marker comment `<!-- ledger-sync: id=… list=… -->`.
A re-run matches on it, updates the body only when it changed, and creates nothing
twice. Closing is NOT done here — an issue closes when its row leaves the ledger,
which is a separate step that must cite the commit that did it.
"""
import argparse
import json
import subprocess
import sys

REPO = "victor-smirnov/logos"
LEDGER_FILE = {
    "backlog": "tests/logos/unrowed_backlog.ledger",
    "squeue": "tests/logos/soundness_queue.ledger",
    "bc-admits": "tests/logos/bc_admits.ledger",
}
NOT_A_ROW = (
    "> ⚠ **This is not a ledger row and must not be priced as one.**\n"
    "> A backlog entry names the ONE measurement that turns it into a row — or throws\n"
    "> it away. It may turn out to be zero defects. Take the measurement first.\n"
)


def run(args: list[str]) -> tuple[int, str, str]:
    """Never `cmd | head; echo $?` — that reports head's status. Capture, then read rc."""
    p = subprocess.run(args, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def rows_for(which: str) -> tuple[list[dict], str]:
    rc, out, err = run(["python3", "scripts/arc-progress.py", "--rows"])
    if rc != 0:
        sys.exit(f"arc-progress --rows refused (rc {rc}): {err.strip()}")
    data = json.loads(out)
    return [r for r in data["rows"] if r["list"] == which], data["head"]


def labels_for(row: str, r: dict) -> list[str]:
    out = [f"ledger:{row}"]
    if row == "backlog":
        out += [f"kind:{r['kind']}", "needs-measurement"]
    elif row == "squeue":
        out.append(f"tier:{r['tier']}")
    return out


OBSERVED_LEGEND = {
    "run": "the program COMPILES CLEAN and RUNS, and exits with that code where rustc exits 0. "
           "The oracle is the RUN, not the compile.",
    "admits": "logosc ACCEPTS a program rustc rejects.",
    "refuses": "logosc REFUSES a program rustc accepts and runs.",
    "diag": "the verdict is right and the sentence is wrong.",
}


def program_header(program: str) -> list[str]:
    """The program's own leading comment block — the defect in its author's words.
    Quoting the tree beats paraphrasing it, and a one-line row is unreadable without
    it. Missing file or no header: say nothing rather than invent."""
    for cand in (f"{program}.logos", program):
        try:
            with open(cand) as fh:
                out = []
                for ln in fh:
                    s = ln.strip()
                    if s.startswith("//"):
                        out.append(s.lstrip("/ ").rstrip())
                    elif not s:
                        if out:
                            break
                    else:
                        break
                return [l for l in out if l]
        except (FileNotFoundError, IsADirectoryError, UnicodeDecodeError):
            continue
    return []


def body_for(row: str, r: dict, head: str) -> str:
    verbatim = "\n".join(r.get("prose", []))
    # (no `src` here any more — the provenance names the FILE, and a stray
    # "file:line" local is what shadowed the program source once already)
    parts = [NOT_A_ROW if row == "backlog" else "", f"### The entry, verbatim from `{r['file']}`\n"]
    # PRINT THE LINE AS IT STANDS. An earlier version re-assembled it from the
    # parsed columns and silently dropped one, while the section above it still
    # promised "verbatim" — a quotation that is not one is worse than a summary.
    index_line = r.get("raw") or f"{r['id']}  (raw line unavailable)"
    parts.append("```\n" + index_line + "\n"
                 + (verbatim + "\n" if verbatim else "") + "```\n")
    if r.get("program"):
        parts.append(f"**Program:** `{r['program']}`\n")
        hdr = program_header(r["program"])
        if hdr:
            parts.append("### What the program says about itself\n> "
                         + "\n> ".join(hdr) + "\n")
        else:
            # 40 of the 236 queue programs carry no leading `//` header (they open
            # straight at `package …`). Without one the issue is an id and a path,
            # which is useless to anyone outside this tree — so quote the program
            # itself, bounded. It IS the reproducer; quoting beats paraphrasing.
            for cand in (f"{r['program']}.logos", r["program"]):
                try:
                    # ⚠ NOT `src` — that name already holds "file:line" for the
                    # provenance footer below, and shadowing it printed the whole
                    # program source where the path belongs. Caught by reading a
                    # rendered body, not by reading this code.
                    prog_lines = open(cand).read().splitlines()
                except (FileNotFoundError, IsADirectoryError, UnicodeDecodeError):
                    continue
                shown, clipped = prog_lines[:40], len(prog_lines) > 40
                parts.append("### The program (no header comment; quoted from the tree)\n"
                             + "```rust\n" + "\n".join(shown) + "\n"
                             + (f"… {len(prog_lines) - 40} more lines, see the file\n"
                                if clipped else "")
                             + "```\n")
                break
    obs = (r.get("observed") or "").split()
    if obs and obs[0] in OBSERVED_LEGEND:
        parts.append(f"**Observed:** `{r['observed']}` — {OBSERVED_LEGEND[obs[0]]}\n")
    if row == "squeue" and r.get("tier"):
        parts.append(f"**Tier {r['tier']}** — the tiers are defined in the header of "
                     f"`{LEDGER_FILE[row]}`; tiers 1–2 are wrong behaviour at run time, "
                     "3–4 are a legal program refused or a wrong sentence.\n")
    parts.append(
        "### How this closes\n"
        f"Only by the entry disappearing from `{LEDGER_FILE[row]}` in a commit that states why.\n"
        "**Do not close this issue by hand** — the issue would close while the gate stayed red.\n"
    )
    # ⚠ FILE, NOT file:line. A line number is a FRAGILE field: the normalising write
    # shifted every backlog entry down, which made all 18 issues report "changed" for
    # one digit apiece. At 236 rows that is a mass update per renumbering, and a diff
    # full of noise hides the one change that matters. The id is the key; it locates
    # the row without depending on where it sits.
    # ⚠ AND NO COMMIT SHA EITHER. Embedding HEAD made every one of the 314 issues
    # report "changed" after any commit — caught when a commit landed WHILE the 236
    # were being published, so freshly generated bodies already disagreed with
    # freshly created ones. Same fragile-field lesson as the line number a few edits
    # ago: I removed that one and left a field beside it that moves even more often.
    # `head` stays in the signature (callers pass it) but must not reach the body.
    parts.append(f"\n<sub>Projected from `{r['file']}`. The ledger is the source of "
                 "truth; this issue is a mirror. ⚠ Paths under `/home/` or `sandbox/` name "
                 "scratch probes on the development box and will not exist in a clone — the "
                 "entry's own measurement is the reproducible part.</sub>\n")
    parts.append(f"\n<!-- ledger-sync: id={r['id']} list={row} -->")
    return "\n".join(p for p in parts if p)


def existing() -> dict[str, dict]:
    rc, out, err = run(["gh", "issue", "list", "--repo", REPO, "--state", "all",
                        "--limit", "1000", "--json", "number,title,body"])
    if rc != 0:
        sys.exit(f"gh issue list failed (rc {rc}): {err.strip()}")
    found = {}
    for it in json.loads(out):
        for line in (it.get("body") or "").splitlines():
            if line.startswith("<!-- ledger-sync:"):
                key = line.split("id=", 1)[1].split()[0]
                found[key] = it
    return found


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", dest="which", required=True, choices=sorted(LEDGER_FILE))
    ap.add_argument("--apply", action="store_true", help="actually create/update issues")
    ap.add_argument("--limit", type=int, default=0, help="stop after N (0 = all)")
    a = ap.parse_args()

    rows, head = rows_for(a.which)
    if a.limit:
        rows = rows[:a.limit]
    have = existing()
    create = update = same = 0

    for r in rows:
        title = f"[{a.which}] {r['id']}"
        body = body_for(a.which, r, head)
        labels = labels_for(a.which, r)
        it = have.get(r["id"])
        if it is None:
            create += 1
            print(f"CREATE  {title}  [{', '.join(labels)}]")
            if a.apply:
                cmd = ["gh", "issue", "create", "--repo", REPO, "--title", title, "--body", body]
                for l in labels:
                    cmd += ["--label", l]
                rc, out, err = run(cmd)
                if rc != 0:
                    print(f"  FAILED rc={rc}: {err.strip()[:200]}", file=sys.stderr)
                else:
                    print(f"  -> {out.strip()}")
        elif (it.get("body") or "").strip() != body.strip():
            update += 1
            print(f"UPDATE  #{it['number']}  {title}")
            if a.apply:
                rc, out, err = run(["gh", "issue", "edit", str(it["number"]),
                                    "--repo", REPO, "--body", body])
                if rc != 0:
                    print(f"  FAILED rc={rc}: {err.strip()[:200]}", file=sys.stderr)
        else:
            same += 1

    print(f"\n{a.which}: {len(rows)} rows — create {create}, update {update}, unchanged {same}"
          + ("" if a.apply else "   (DRY RUN — pass --apply to write)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
