#!/usr/bin/env bash
# layout_decline_ledger_gate.sh LOGOSC LEDGER PASS_DIR
#
# THE OPEN LAYOUT DECLINES ARE A LEDGER, AND A LEDGER MUST BE HELD IN BOTH
# DIRECTIONS OR IT IS AN EXCLUSION LIST WEARING A LEDGER'S NAME.
#
# Every `pass` corpus test compiles under `LOGOS_VERIFY_LAYOUT=1` (see
# tests/logos/CMakeLists.txt), so an engine that declines to size a type is a
# red test at the program that causes it. The programs in `layout_decline.ledger`
# are the exception: they are excluded BY NAME because the decline is real and
# its cause is in another phase. That exclusion is only honest if something
# re-measures it, so this gate does:
#
#   * a listed program that declines FEWER times than recorded, or stops
#     declining, or declines a DIFFERENT type → RED. A fixed defect must delete
#     its entry; a ledger that silently keeps a stale row is the same blindness
#     one level up.
#   * a listed program that declines MORE times → RED.
#   * a listed program that no longer exists → RED.
#
# ⚠ AND THE GATE PROVES ITS OWN INSTRUMENT IN THE SAME RUN. `LOGOS_LAYOUT_CANARY
# =declined` pushes ONE synthetic decline through `record_declined`, so a listed
# program compiled with it MUST report exactly one more decline than recorded.
# If it does not, the reading apparatus — the env var reaching the compiler, the
# `[declined]` rows reaching stderr, this file's parse of them — is dead, and
# the gate reports ITSELF broken rather than reporting the ledger clean.
# Whatever blinds the real measurement blinds the canary, because it is the same
# measurement.
set -euo pipefail

LOGOSC="${1:?logosc}"
LEDGER="${2:?ledger file}"
PASS_DIR="${3:?pass corpus dir}"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fail=0
n_entries=0

# Count the `[declined] <key>: DECLINED` rows for one program. Prints the count.
# ⚠ NOT `<compiler stderr> | grep -c`: under `pipefail` a `grep` that exits early
# turns the producer's SIGPIPE into the pipeline's status. Materialise, then match.
declines_for() {
    local src="$1" key="$2" canary="${3:-}"
    local err="$TMPD/err.txt"
    if [ -n "$canary" ]; then
        LOGOS_VERIFY_LAYOUT=1 LOGOS_LAYOUT_CANARY="$canary" \
            "$LOGOSC" "$src" -o "$TMPD/out.o" >/dev/null 2>"$err" || true
    else
        LOGOS_VERIFY_LAYOUT=1 \
            "$LOGOSC" "$src" -o "$TMPD/out.o" >/dev/null 2>"$err" || true
    fi
    grep -c -F "[declined] ${key}: DECLINED" "$err" || true
}

while IFS= read -r line; do
    line="${line%%#*}"
    # shellcheck disable=SC2086
    set -- $line
    [ "$#" -eq 0 ] && continue
    if [ "$#" -ne 3 ]; then
        echo "GATE BROKEN: malformed ledger line (want '<name> <count> <key>'): $line"
        exit 4
    fi
    name="$1"; want="$2"; key="$3"
    n_entries=$((n_entries + 1))

    src="$PASS_DIR/$name.logos"
    if [ ! -f "$src" ]; then
        echo "FAIL: ledger names '$name', but $src does not exist."
        echo "      A ledger entry for a program that is gone is a claim nobody can check."
        fail=1
        continue
    fi

    got=$(declines_for "$src" "$key")
    if [ "$got" != "$want" ]; then
        if [ "$got" = 0 ]; then
            echo "FAIL: $name no longer declines '$key' — the defect is FIXED."
            echo "      Delete its entry from $LEDGER and drop it from"
            echo "      LAYOUT_DECLINE_LEDGER in tests/logos/CMakeLists.txt, so the"
            echo "      program is verified with the rest of the corpus."
        elif [ "$got" -lt "$want" ]; then
            echo "FAIL: $name declines '$key' $got time(s), ledger records $want."
            echo "      The ledger may only SHRINK by being edited, never by drifting."
        else
            echo "FAIL: $name declines '$key' $got time(s), ledger records $want — it GREW."
        fi
        fail=1
        continue
    fi

    # ⚠ THE CANARY, on this same program, in this same invocation: one injected
    # decline must be visible as exactly one more row of the SAME key we count.
    # `LOGOS_LAYOUT_CANARY=declined` records under the key `<fault-injected>`, so
    # the canary is read on ITS key while the real count is read on the ledger's.
    cgot=$(declines_for "$src" "<fault-injected>" declined)
    if [ "$cgot" -lt 1 ]; then
        echo "GATE BROKEN: the injected decline was not observed on '$name'."
        echo "  LOGOS_LAYOUT_CANARY=declined must produce a '[declined] <fault-injected>'"
        echo "  row. It did not, so this gate cannot see a decline at all and its"
        echo "  'ledger clean' verdict is about a measurement that did not happen."
        exit 4
    fi
done < "$LEDGER"

if [ "$n_entries" -eq 0 ]; then
    # An empty ledger is the GOAL, and it is also indistinguishable from a
    # ledger this script failed to read. Prove the instrument on a program that
    # is guaranteed to decline under the canary, then report the empty ledger.
    # ⚠ NOT `ls … | head -1`. Under `set -o pipefail` `head` closes the pipe
    # after one line, `ls` dies of SIGPIPE, and the pipeline's status is 141 —
    # which `set -e` turns into an exit-141 that reads as a crashed gate rather
    # than as "the ledger is empty". MEASURED here, first run. Glob, don't pipe.
    probes=()
    probes=("$PASS_DIR"/*.logos)
    probe="${probes[0]}"
    cgot=$(declines_for "$probe" "<fault-injected>" declined)
    if [ "$cgot" -lt 1 ]; then
        echo "GATE BROKEN: ledger is empty AND the injected decline was not observed."
        exit 4
    fi
    echo "OK: layout-decline ledger is EMPTY (instrument proved live on $(basename "$probe"))."
    exit 0
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "OK: layout-decline ledger holds — $n_entries entry/entries, each re-measured,"
echo "    instrument proved live by an injected decline in the same run."
