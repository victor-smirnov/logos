#!/usr/bin/env bash
# mlir_gen_bug_ledger_gate.sh LOGOSC LEDGER REPO_ROOT
#
# THE OPEN mlir-gen SELF-DIAGNOSES ARE A LEDGER, AND A LEDGER MUST BE HELD IN
# BOTH DIRECTIONS OR IT IS AN EXCLUSION LIST WEARING A LEDGER'S NAME.
#
# R2: a message mlir-gen emits ABOUT ITSELF ("statement DROPPED", "unknown
# tagged enum", "unhandled expr code") must make the compile FAIL — before that
# rule the compiler printed such a line, wrote the object file and returned 0,
# so a test could pass while a WRITE had silently vanished. Every `pass` corpus
# test therefore compiles with the rule live, and a program that trips it is a
# red test at the program that causes it.
#
# The programs in mlir_gen_bug.ledger are the exception, and THIS FILE IS THE
# ONLY PLACE THEY ARE NAMED. Every test is handed the ledger's PATH
# (LOGOS_MLIRGEN_BUG_LEDGER, set unconditionally in tests/logos/CMakeLists.txt)
# and logosc asks whether the program it is compiling is listed here — so the
# variable names the ledger, it does not grant the excuse, and a shell that
# exports it cannot silence the rule for anything the ledger does not name.
# They are excluded because the malfunction is real and its cause is in
# another phase. That
# exclusion is only honest if something re-measures it, so this gate does:
#
#   * a listed program that trips FEWER times than recorded, or stops
#     tripping → RED. A fixed defect must DELETE its entry; a ledger that
#     silently keeps a stale row is the same blindness one level up.
#   * a listed program that trips MORE times → RED.
#   * a listed program whose source no longer exists → RED.
#
# ⚠ AND THE GATE PROVES ITS OWN INSTRUMENT IN THE SAME RUN. `LOGOS_MLIRGEN_CANARY=1`
# pushes ONE synthetic malfunction through the SAME sink, so a listed program
# compiled with it MUST report exactly one more than recorded. If it does not,
# the reading apparatus — the env var reaching the compiler, the `internal:`
# rows reaching stderr, this file's parse of them — is dead, and the gate
# reports ITSELF broken rather than reporting the ledger clean.
set -euo pipefail

LOGOSC="${1:?logosc}"
LEDGER="${2:?ledger file}"
ROOT="${3:?repo root}"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fail=0
n_entries=0

# Count the `mlir_gen: internal:` rows for one program. Prints the count.
# ⚠ NOT `<compiler stderr> | grep -c`: under `pipefail` a `grep` that exits
# early turns the producer's SIGPIPE into the pipeline's status. Materialise,
# then match.
bugs_for() {
    local src="$1" canary="${2:-}"
    local err="$TMPD/err.txt"
    if [ -n "$canary" ]; then
        LOGOS_MLIRGEN_CANARY=1 \
            "$LOGOSC" "$src" -o "$TMPD/out.o" >/dev/null 2>"$err" || true
    else
        "$LOGOSC" "$src" -o "$TMPD/out.o" >/dev/null 2>"$err" || true
    fi
    grep -c -F "mlir_gen: internal:" "$err" || true
}

while IFS= read -r line; do
    line="${line%%#*}"
    # shellcheck disable=SC2086
    set -- $line
    [ "$#" -eq 0 ] && continue
    if [ "$#" -ne 3 ]; then
        echo "GATE BROKEN: malformed ledger line (want '<name> <count> <path>'): $line"
        exit 4
    fi
    name="$1"; want="$2"; rel="$3"
    n_entries=$((n_entries + 1))

    src="$ROOT/$rel.logos"
    if [ ! -f "$src" ]; then
        echo "FAIL: ledger names '$name' at '$rel', but $src does not exist."
        echo "      A ledger entry for a program that is gone is a claim nobody can check."
        fail=1
        continue
    fi

    got=$(bugs_for "$src")
    if [ "$got" != "$want" ]; then
        if [ "$got" = 0 ]; then
            echo "FAIL: $name no longer self-diagnoses — the defect is FIXED."
            echo "      Delete its row from $LEDGER — that is the ONLY place it"
            echo "      is named, so the program is then held to the rule with"
            echo "      the rest of the corpus."
        elif [ "$got" -lt "$want" ]; then
            echo "FAIL: $name self-diagnoses $got time(s), ledger records $want."
            echo "      The ledger may only SHRINK by being edited, never by drifting."
        else
            echo "FAIL: $name self-diagnoses $got time(s), ledger records $want — it GREW."
        fi
        fail=1
        continue
    fi

    # ⚠ THE CANARY, on this same program, in this same invocation: one injected
    # malfunction must be visible as exactly one more row on the same channel.
    cgot=$(bugs_for "$src" canary)
    if [ "$cgot" -ne $((want + 1)) ]; then
        echo "GATE BROKEN: the injected malfunction was not observed on '$name'."
        echo "  LOGOS_MLIRGEN_CANARY=1 must add exactly one 'mlir_gen: internal:'"
        echo "  row (expected $((want + 1)), saw $cgot). It did not, so this gate"
        echo "  cannot see a self-diagnosis at all and its 'ledger clean' verdict"
        echo "  is about a measurement that did not happen."
        exit 4
    fi
done < "$LEDGER"

if [ "$n_entries" -eq 0 ]; then
    # An empty ledger is the GOAL, and it is also indistinguishable from a
    # ledger this script failed to read. Prove the instrument on a program that
    # is guaranteed to trip under the canary, then report the empty ledger.
    probes=()
    probes=("$ROOT"/tests/logos/pass/*.logos)
    probe="${probes[0]}"
    cgot=$(bugs_for "$probe" canary)
    if [ "$cgot" -lt 1 ]; then
        echo "GATE BROKEN: ledger is empty AND the injected malfunction was not observed."
        exit 4
    fi
    echo "OK: mlir-gen self-diagnosis ledger is EMPTY (instrument proved live on $(basename "$probe"))."
    exit 0
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "OK: mlir-gen self-diagnosis ledger holds — $n_entries entry/entries, each"
echo "    re-measured, instrument proved live by an injected malfunction in the same run."
