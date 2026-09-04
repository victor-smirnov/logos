#!/usr/bin/env bash
# soundness_queue_gate.sh LOGOSC LEDGER REPO_ROOT
#
# THE OPEN SOUNDNESS DEFECTS ARE A LEDGER, AND A LEDGER MUST BE HELD IN BOTH
# DIRECTIONS OR IT IS A SKIP LIST WEARING A LEDGER'S NAME.
#
# ── WHAT IS IN IT ───────────────────────────────────────────────────────────
# A row of `soundness_queue.ledger` is a program logosc compiles clean and that
# is WRONG — it double-frees, reads freed memory, computes garbage, accepts a
# body no checker ever looked at — or (tiers 3-4) a legal program the compiler
# refuses, or an illegal one refused with the wrong sentence. None of these can
# be an ordinary fixture: a pass fixture may not assert a program that ABORTS,
# and a fail fixture asserting a refusal of legal Rust would be pinning the
# defect as the rule. So each is ONE ROW, its program on the shelf under
# tests/soundness/open/, and THIS GATE is the only place the set is named.
#
# Before this gate the same set lived in prose — a scratchpad list, PROBES.md
# sections, a hand-written block in a progress page — and that failed twice in
# four days: a defect was recorded under an innocent program (`wrap<F>` when the
# real one was `(*b)()`), and a "five owner fixtures wall this" verdict was
# carried six rounds after four of the five had been repaired.
#
# ── WHAT IT ASSERTS, IN BOTH DIRECTIONS ─────────────────────────────────────
#
#   * a listed program that STOPS exhibiting its recorded wrong behaviour → RED.
#     That is the direction that matters: a fix makes this gate go red BECAUSE
#     IT IMPROVED, and the fixed defect must DELETE its row — the program is
#     then held to the rule with the rest of the corpus, as a pass or fail
#     fixture, in the same commit. A defect that stopped reproducing by itself
#     was either fixed by accident (still a fixture) or was never real.
#   * a listed program whose source no longer exists → RED. A row for a program
#     that is gone is a claim nobody can check.
#   * a program on the shelf with NO row → RED. The count is otherwise honest
#     about the wrong set.
#   * the ROW COUNT drifting away from the `# TOTAL n` line → RED. Rows can only
#     be added or removed by editing the total in the same commit, so neither
#     direction of drift is silent.
#
# ⚠ AND THE GATE PROVES ITS OWN INSTRUMENT IN THE SAME RUN. Every verdict here
# is read off ONE reader — compile the way the pass tier does, link, run — and
# on a broken reader (a compiler that cannot be found, a link that always fails,
# a binary that never runs) every `run` row reads "no longer reproduces" or
# every `refuses` row reads "still refused", depending on which half broke.
# Three planted programs — one that MUST run and exit 0, one that MUST run and
# exit 3, one that MUST be refused — are pushed through the SAME `observe()`
# used on every row. If the reader cannot tell them apart it reports ITSELF
# broken (exit 4) rather than reporting the queue clean or a row closed.
#
# ⚠ THIS GATE COMPILES AND RUNS EVERY ROW, SERIALLY, IN ONE ctest SLOT. That is
# the ONE SCHEDULER rule (a registered test may not fan out its own workers) and
# it is affordable here BECAUSE THE FILE IS MEANT TO SHRINK: 18 rows measured
# ~1.5 s each on 2026-09-04. The day it holds hundreds, do what the bc ledger did
# (one registered test per program, roster here) — not a `-P`.
set -euo pipefail

LOGOSC="${1:?logosc}"
LEDGER="${2:?ledger file}"
ROOT="${3:?repo root}"
SHELF="$ROOT/tests/soundness/open"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

# The pass tier compiles every fixture under LOGOS_VERIFY_LAYOUT=1; so does this
# reader, so a compiler crash the corpus would meet is a `refuses` here and a
# row can say so (layout_verify_recursive_ref is exactly that).
export LOGOS_VERIFY_LAYOUT=1

LINK_ARCHIVES=()
if [ -n "${LOGOS_LIB_DIR:-}" ]; then
    # Same archive order as run_test.sh: monolith first, layer archives, rest.
    for a in "$LOGOS_LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done
    for a in "$LOGOS_LIB_DIR"/liblogos-*.a;  do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done
    for a in "$LOGOS_LIB_DIR"/*.a; do
        case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && LINK_ARCHIVES+=("$a") ;; esac
    done
fi

# observe SRC — THE ONE READER. Sets:
#   O_CC    the compiler's exit code
#   O_DIAG  1 if stderr carries a diagnostic (`error:` / `error [`), else 0
#   O_RUN   the linked binary's exit code, or "-" when nothing ran
#   O_ERR   path of the compiler's stderr, for `diag` rows and for messages
# ⚠ NOT `<stderr> | grep -q`: under `pipefail` a `grep` that exits early turns
# the producer's SIGPIPE into the pipeline's status. Materialise, then match.
# `error( \[|:)` rather than a bare "error", which would also match the word
# inside a WARNING's own text.
observe() {
    local src="$1" d
    d=$(mktemp -d -p "$TMPD")
    O_ERR="$d/cc.err"; O_RUN="-"; O_CC=0; O_DIAG=0
    "$LOGOSC" "$src" -o "$d/t.o" >"$d/cc.out" 2>"$O_ERR" || O_CC=$?
    grep -q -E "error( \[|:)" "$O_ERR" && O_DIAG=1
    if [ "$O_CC" -ne 0 ] || [ "$O_DIAG" -ne 0 ]; then return 0; fi
    if ! cc "$d/t.o" -Wl,--start-group "${LINK_ARCHIVES[@]}" -Wl,--end-group \
            -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
            -o "$d/t" 2>"$d/ld.err"; then
        # A link failure is never a defect closing; it is the READER (no archives,
        # a stdlib that does not match the compiler). Never a row verdict.
        O_RUN="LINKFAIL"; return 0
    fi
    # A row's program may abort (134) or segfault (139); that IS the measurement.
    # `timeout` so a program that hangs is a number too, not a stuck gate.
    O_RUN=0
    timeout 60 "$d/t" >"$d/stdout" 2>/dev/null || O_RUN=$?
}

# ── THE CANARY, BEFORE ANY VERDICT ──────────────────────────────────────────
# ⚠ The clean canary USES THE STDLIB (a String), so a reader with no archives on
# its link line — LOGOS_LIB_DIR unset, the measured way this gate was first run
# wrong — fails HERE, as "gate broken", and not row by row as "no longer
# reproduces" on every program that allocates.
cat > "$TMPD/canary_runs.logos" <<'EOP'
package soundness_canary_runs;
use logos.mem.string;
fn main() -> i32 {
    let s: String = String::from("ab");
    let mut v: i64 = s.len();
    v = v + 1i64;
    return v as i32 - 3i32;
}
EOP
cat > "$TMPD/canary_exits3.logos" <<'EOP'
package soundness_canary_exits3;
fn main() -> i32 {
    let mut v: i64 = 1i64;
    v = 2i64;
    return v as i32 + 1i32;
}
EOP
cat > "$TMPD/canary_refused.logos" <<'EOP'
package soundness_canary_refused;
fn main() -> i32 {
    let v: i64 = 1i64;
    v = 2i64;
    return 0i32;
}
EOP
observe "$TMPD/canary_runs.logos";   c_runs="$O_CC/$O_DIAG/$O_RUN"
observe "$TMPD/canary_exits3.logos"; c_ex3="$O_CC/$O_DIAG/$O_RUN"
observe "$TMPD/canary_refused.logos"; c_ref="$O_CC/$O_DIAG/$O_RUN"
if [ "$c_runs" != "0/0/0" ] || [ "$c_ex3" != "0/0/3" ] || [ "$c_ref" = "0/0/0" ] || [ "${c_ref#0/0/}" != "$c_ref" ]; then
    echo "GATE BROKEN: the planted programs were not classified correctly by the"
    echo "  same reader that judges every row below (cc/diag/run). A clean program"
    echo "  that must exit 0 read '$c_runs' (want 0/0/0); one that must exit 3 read"
    echo "  '$c_ex3' (want 0/0/3); one that assigns twice to an immutable local read"
    echo "  '$c_ref' (want a non-zero cc or diag=1, nothing run). On this reader a"
    echo "  'run' row would read as closed, or a 'refuses' row as open, for reasons"
    echo "  that have nothing to do with the compiler — so no verdict below is one."
    exit 4
fi
echo "[soundness] canary: a clean program ran and exited 0, a wrong one exited 3, a refusal read as refused"

if [ ! -f "$LEDGER" ]; then
    echo "GATE BROKEN: ledger '$LEDGER' does not exist, so the rows it was"
    echo "  meant to hold are held by nothing at all."
    exit 4
fi

fail=0
want_total=""
n_entries=0
seen_rel=""
declare -A by_tier=()

while IFS= read -r line; do
    case "$line" in
        \#\ TOTAL\ *) want_total="${line#\# TOTAL }" ;;
    esac
    line="${line%%#*}"
    # shellcheck disable=SC2086
    set -- $line
    [ "$#" -eq 0 ] && continue
    if [ "$#" -lt 4 ]; then
        echo "GATE BROKEN: malformed ledger line (want '<id> <tier> <path> <observed...>'): $line"
        exit 4
    fi
    id="$1"; tier="$2"; rel="$3"; kind="$4"; shift 4; arg="$*"
    n_entries=$((n_entries + 1))
    by_tier[$tier]=$(( ${by_tier[$tier]:-0} + 1 ))
    seen_rel="$seen_rel $rel"

    src="$ROOT/$rel.logos"
    if [ ! -f "$src" ]; then
        echo "FAIL: row '$id' names '$rel', but $src does not exist."
        echo "      A row for a program that is gone is a claim nobody can check."
        fail=1
        continue
    fi

    observe "$src"
    if [ "$O_RUN" = "LINKFAIL" ]; then
        echo "GATE BROKEN: row '$id' compiled clean and did not LINK. A link failure is"
        echo "  the reader's (archives missing or stale against this compiler), never a"
        echo "  defect closing, so no verdict on any row is one."
        exit 4
    fi
    ok=0; why=""
    case "$kind" in
        admits)
            [ "$O_CC" -eq 0 ] && [ "$O_DIAG" -eq 0 ] && ok=1
            why="the compile is no longer silent (cc=$O_CC diag=$O_DIAG)" ;;
        refuses)
            { [ "$O_CC" -ne 0 ] || [ "$O_DIAG" -ne 0 ]; } && ok=1
            why="the program now compiles clean (cc=$O_CC diag=$O_DIAG run=$O_RUN)" ;;
        run)
            [ "$O_CC" -eq 0 ] && [ "$O_DIAG" -eq 0 ] && [ "$O_RUN" = "$arg" ] && ok=1
            why="want cc=0 diag=0 run=$arg, read cc=$O_CC diag=$O_DIAG run=$O_RUN" ;;
        diag)
            if { [ "$O_CC" -ne 0 ] || [ "$O_DIAG" -ne 0 ]; } && grep -qF -- "$arg" "$O_ERR"; then ok=1; fi
            why="stderr no longer carries '$arg' (cc=$O_CC diag=$O_DIAG run=$O_RUN)" ;;
        *)
            echo "GATE BROKEN: row '$id' has an unknown observed-kind '$kind' (admits|refuses|run <rc>|diag <text>)."
            exit 4 ;;
    esac
    if [ "$ok" -ne 1 ]; then
        echo "FAIL: row '$id' (tier $tier) NO LONGER REPRODUCES — $why."
        echo "      That is a defect CLOSING, and it must not be silent: delete the row,"
        echo "      decrement '# TOTAL', and land $rel.logos as a pass or fail fixture"
        echo "      in the same commit — the row leaving IS the record of the fix. If"
        echo "      nothing in this change was meant to touch it, it closed by accident"
        echo "      or was never real; either way the row may not stand."
        grep -m2 -E "error( \[|:)" "$O_ERR" 2>/dev/null | sed 's/^/        /'
        fail=1
    fi
done < "$LEDGER"

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

# ── THE OTHER DIRECTION: A PROGRAM WITH NO ROW ──────────────────────────────
for f in "$SHELF"/*.logos; do
    [ -e "$f" ] || continue
    rel="${f#"$ROOT"/}"; rel="${rel%.logos}"
    case " $seen_rel " in
        *" $rel "*) ;;
        *) echo "FAIL: $rel is on the open-defects shelf with NO ledger row."
           echo "      Nothing globs this directory, so an unlisted program is checked by"
           echo "      nothing at all — and a count honest about the wrong set is worse"
           echo "      than no count."
           fail=1 ;;
    esac
done

tiers=""
for t in $(printf '%s\n' "${!by_tier[@]}" | sort); do tiers="$tiers tier$t=${by_tier[$t]}"; done
if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "OK: soundness queue holds — $n_entries open row(s) ($tiers), '# TOTAL' says $want_total;"
echo "    every row's program still exhibits its recorded wrong behaviour, every"
echo "    program on the shelf has a row, and the three planted programs prove the"
echo "    reader can tell a clean run from a wrong exit code from a refusal."
