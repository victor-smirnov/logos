#!/usr/bin/env bash
# bc_admits_ledger_gate.sh LOGOSC LEDGER REPO_ROOT [LEDGER...]
#
# THE BORROW-CHECK HOLES FOUND BY THE rustc IMPORT ARE A LEDGER, AND A LEDGER
# MUST BE HELD IN BOTH DIRECTIONS OR IT IS A SKIP LIST WEARING A LEDGER'S NAME.
#
# ── WHAT IS IN IT ───────────────────────────────────────────────────────────
# 983 rustc compile-fail tests (tests/ui/{borrowck,nll,moves,lifetimes,regions,
# dropck,drop}) were ported at upstream `da5114692c9ebe46b869488c5f34f92eb10b98c1`
# (src/version 1.100.0). Each port that logosc REFUSES for the upstream reason
# was landed as an ordinary fail fixture under tests/imported/fail/ — that is
# coverage, and it needs no ledger. Each port that logosc ADMITS is a program
# rustc rejects and we compile: a borrow-check hole, real, with its root
# recorded. There are hundreds; they cannot be hundreds of red tests, and they
# must not be hundreds of files nothing looks at either.
#
# So each is ONE ROW in a ledger, and THOSE LEDGERS ARE THE ONLY PLACE THE
# ADMITTED SET IS NAMED. There are TWO of them since 2026-09-04 and this gate
# takes both: `bc_admits.ledger` is the ACTIONABLE QUEUE (a row is a defect with
# a root) and `bc_admits_blocked.ledger` holds the rows NO COMPILER FIX CLOSES
# — a port that is legal Rust as written, a canonised divergence, a wall of
# legal fixtures only the owner can move. The split is about what a ROUND may
# spend its pricing phase on; it changes NOTHING this gate asserts. The admitted
# set is the union, every program is still on the shelf, and every one still has
# its own `logos_00_bc_admit_*` test. Each file holds its OWN `# TOTAL`, so a
# row cannot be laundered from one file into the other silently either — both
# counts move, in one change, or the gate reds.
# The programs live under tests/imported/admit/<category>/ — deliberately
# NOT under tests/imported/{pass,fail}, which is what `tests/logos/CMakeLists.txt`
# globs and what `corpus_registration_gate.sh` walks: a program there would
# either need an `.expected` asserting the hole is correct, or become an
# unregistered orphan. Their registration is THIS GATE, which names every one.
#
# ── WHAT IT ASSERTS, IN BOTH DIRECTIONS ─────────────────────────────────────
#
#   * a listed program that STOPS being admitted → RED. That is the direction
#     that matters: a class fix makes this gate go red BECAUSE IT IMPROVED, and
#     a fixed defect must DELETE its row — the program is then held to the rule
#     with the rest of the corpus, as a fail fixture.
#   * a listed program that emits ANY diagnostic → RED, same reason. For an
#     ADMIT row "trips more" and "stops tripping" are the same event: the
#     compile stopped being silent.
#   * a listed program whose source no longer exists → RED. A row for a program
#     that is gone is a claim nobody can check.
#   * the ROW COUNT drifting away from the `# TOTAL n` line at the top → RED.
#     Rows can only be added or removed by editing the total in the same commit,
#     so neither direction of drift is silent. (Same discipline as predicting
#     `ctest -N` before a reconfigure rather than explaining it after.)
#
# ⚠ AND THE GATE PROVES ITS OWN INSTRUMENT IN THE SAME RUN. Every finding here
# is "the compile was not silent", and on a broken reader every compile looks
# silent and the whole ledger certifies clean. Two planted programs — one that
# MUST be refused and one that MUST be admitted — are pushed through the SAME
# `admitted()` used on every row. If the refusal reads as admitted, or the
# clean program reads as refused, the gate reports ITSELF broken rather than
# reporting the ledger clean.
set -euo pipefail

LOGOSC="${1:?logosc}"
LEDGER="${2:?ledger file}"
ROOT="${3:?repo root}"
shift 3
# Any further arguments are MORE ledgers over the same shelf (today:
# bc_admits_blocked.ledger). Each is walked by the SAME loop and holds its OWN
# `# TOTAL`; the "every file has a row" sweep at the bottom sees their union.
LEDGERS=("$LEDGER" "$@")

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

# Prints "0" when the program compiled with NO diagnostic, "1" otherwise.
# ⚠ NOT `<compiler stderr> | grep -q`: under `pipefail` a `grep` that exits early
# turns the producer's SIGPIPE into the pipeline's status. Materialise, then match.
# ⚠ ONE READER, NOT TWO. The verdict logic lives in `bc_admit_one.sh`, which the
# 462 per-program tests use; this gate only borrows it for the canary below. A
# second copy here would be the drift this codebase has paid for twice — two
# walkers of one fact that diverge.
admitted() {
    "$(dirname "$0")/bc_admit_one.sh" "$LOGOSC" "$1" canary >/dev/null 2>&1 \
        && echo 0 || echo 1
}

# ── THE CANARY, BEFORE ANY VERDICT ──────────────────────────────────────────
cat > "$TMPD/canary_refused.logos" <<'EOF'
package bc_admits_canary_refused;
fn main() -> i32 {
    let v: i64 = 1i64;
    v = 2i64;
    return 0i32;
}
EOF
cat > "$TMPD/canary_admitted.logos" <<'EOF'
package bc_admits_canary_admitted;
fn main() -> i32 {
    let mut v: i64 = 1i64;
    v = 2i64;
    return v as i32 - 2i32;
}
EOF
c_ref=$(admitted "$TMPD/canary_refused.logos")
c_adm=$(admitted "$TMPD/canary_admitted.logos")
if [ "$c_ref" != 1 ] || [ "$c_adm" != 0 ]; then
    echo "GATE BROKEN: the planted programs were not classified correctly by the"
    echo "  same reader that judges every row below. A program that assigns twice"
    echo "  to an immutable local read as '$c_ref' (want 1 = refused), and a clean"
    echo "  program read as '$c_adm' (want 0 = admitted). Every verdict this gate"
    echo "  can reach is 'the compile was silent'; on a reader that cannot see a"
    echo "  refusal that is true of everything, so 'ledger clean' below would be"
    echo "  about a measurement that did not happen."
    exit 4
fi
echo "[bc-admits] canary: a refusal read as refused and a clean compile read as clean"

fail=0
seen_rel=""

for LEDGER in "${LEDGERS[@]}"; do
if [ ! -f "$LEDGER" ]; then
    echo "GATE BROKEN: ledger '$LEDGER' does not exist, so the rows it was"
    echo "  meant to hold are held by nothing at all."
    exit 4
fi
want_total=""
n_entries=0

while IFS= read -r line; do
    case "$line" in
        \#\ TOTAL\ *) want_total="${line#\# TOTAL }" ;;
    esac
    line="${line%%#*}"
    # shellcheck disable=SC2086
    set -- $line
    [ "$#" -eq 0 ] && continue
    if [ "$#" -ne 3 ]; then
        echo "GATE BROKEN: malformed ledger line (want '<fixture> <root-id> <path>'): $line"
        exit 4
    fi
    name="$1"; root="$2"; rel="$3"
    n_entries=$((n_entries + 1))

    src="$ROOT/$rel.logos"
    if [ ! -f "$src" ]; then
        echo "FAIL: ledger names '$name' at '$rel', but $src does not exist."
        echo "      A ledger entry for a program that is gone is a claim nobody can check."
        fail=1
        continue
    fi

    # ⚠ THE VERDICT IS NOT TAKEN HERE ANY MORE — 2026-08-26. This gate used to
    # compile every row, 462 programs in ONE ctest slot: ~7 min with one core
    # busy and thirty-one idle, and it sits in `tier_commit`, so every gates run
    # paid it. That is #85 seen from its other side — a registered test may not
    # fan out, so a test with N programs to get through runs them SERIALLY and
    # no flag fixes it. Each program is now its own registered test
    # (`logos_00_bc_admit_<dir>_<name>`, run by `bc_admit_one.sh`), which the
    # scheduler that owns the work parallelises: MEASURED 33.5 s wall for all
    # 462, against ~7 min, and a closed hole now NAMES its program instead of
    # reporting that the fold disagreed.
    #
    # What is left here is the ROSTER, which no per-program test can hold: every
    # row has a file, every file has a row, and the count matches `# TOTAL`.
    seen_rel="$seen_rel $rel"
done < "$LEDGER"

# ── EACH FILE'S OWN COUNT, CHECKED BEFORE THE NEXT FILE IS READ ─────────────
# ⚠ PER FILE, NOT SUMMED. A sum over both ledgers would be blind to exactly the
# move this split makes possible: a row silently walked out of the queue and
# into the blocked list keeps the sum, and "no compiler fix closes this" is the
# one claim in this tree that must never be able to arrive without an edit
# somebody signed. Two counts, two `# TOTAL` lines, both move or the gate reds.
if [ -z "$want_total" ]; then
    echo "GATE BROKEN: $LEDGER carries no '# TOTAL <n>' line, so its row count is"
    echo "  held against nothing and rows can appear or vanish silently."
    exit 4
fi
if [ "$n_entries" -ne "$want_total" ]; then
    echo "FAIL: $LEDGER has $n_entries rows, its '# TOTAL' line says $want_total."
    echo "      A row may only be added or removed by editing that number in the same"
    echo "      change — neither direction of drift is allowed to be silent."
    fail=1
fi
echo "[bc-admits] $(basename "$LEDGER"): $n_entries row(s), '# TOTAL' says $want_total"
n_all=$((${n_all:-0} + n_entries))
done

# ── THE OTHER DIRECTION: A PROGRAM WITH NO ROW ──────────────────────────────
# The loop above checks every ROW has a file. Without this, a program could be
# added to the admit shelf, get its own registered test (the glob picks it up),
# and never appear in the ledger — so the COUNT would be honest about a set that
# is not the set on disk. This is the half a per-program test cannot hold.
# ⚠ THE UNION OF THE LEDGERS, NOT THE QUEUE ALONE. A blocked row's program is
# still on the shelf and still has its `logos_00_bc_admit_*` test; the split
# moved where the row is NAMED, not what is checked.
for f in "$ROOT"/tests/imported/admit/*/*.logos; do
    [ -e "$f" ] || continue
    rel="${f#"$ROOT"/}"; rel="${rel%.logos}"
    case " $seen_rel " in
        *" $rel "*) ;;
        *) echo "FAIL: $rel is on the admit shelf with NO ledger row."
           echo "      It has a registered test by the glob, so it is being CHECKED —"
           echo "      but the ledger is where the admitted set is NAMED, and a count"
           echo "      that is honest about the wrong set is worse than no count."
           fail=1 ;;
    esac
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "OK: borrow-check admit ledger ROSTER holds — $n_all row(s) over"
echo "    ${#LEDGERS[@]} ledger(s): every row has a file, every file on the shelf"
echo "    has a row in exactly one of them, and each file's count matches its own"
echo "    '# TOTAL' line."
echo "    ⚠ THIS GATE COMPILES NOTHING. Whether each program is STILL ADMITTED"
echo "    is asserted by its own registered test — logos_00_bc_admit_<dir>_<name>,"
echo "    $n_all of them (the 33.5 s parallel figure against this loop's ~7 min was"
echo "    MEASURED 2026-08-26 on the 462 rows of the day). The two planted programs"
echo "    prove the shared reader can still tell a refusal from a clean compile."
