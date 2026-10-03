#!/usr/bin/env bash
# souffle_oracle_gate.sh LOGOSC PASSDIR — Soufflé as an INDEPENDENT oracle for
# Deem's Datalog (Victor 10-03: Soufflé is an architectural oracle).
#
# Every pass fixture with a `rel` block is compiled with LOGOS_DEEM_ORACLE: the
# deem handler writes `<q>.dl` for each deem whose program lies in the shared
# fragment (stdlib/mem/wql/dl_export.logos), and the generated `<q>` dumps its
# inputs (`<q>/*.facts`) and its answer (`<q>/deem.out`) on every call — the
# last call wins. The fixture must still pass (run_test.sh). Then
# `/usr/bin/souffle <q>.dl -F <q>` runs on the dumped facts and its `__out` is
# compared with Deem's answer AS A SET (Soufflé relations are sets; Deem's
# entry may repeat rows or order them).
#
# Not compared, and counted: a deem the program never called (no deem.out), and
# a call that returned Err (deem.err): Deem's arithmetic is checked, Soufflé's
# wraps, so an error is outside the shared semantics.
#
# ⚠ THE POPULATION IS PINNED FROM BELOW. A change that pushes programs out of
# the fragment would make the gate compare less and stay green; COMPARED_FLOOR
# turns that into a red. Raise it when the population grows.
set -uo pipefail
LOGOSC="$1"
PASS="$2"
# `SOUFFLE` may be overridden only to bite-check the gate with a perturbing
# wrapper; the oracle is the system Soufflé (2.5, 64-bit word = Deem's i64).
SOUFFLE="${SOUFFLE:-/usr/bin/souffle}"
COMPARED_FLOOR=21   # 2026-10-03 first run: 22 exported, 21 compared, 1 Err (div_guard)
ROWS_FLOOR=51       # 2026-10-03 first run
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ ! -x "$SOUFFLE" ]; then
    echo "SKIP: $SOUFFLE not installed — the oracle cannot run"
    exit 77
fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
export LC_ALL=C

fixtures=()
for f in "$PASS"/wql_*.logos "$PASS"/deem_*.logos; do
    grep -qE '^\s*rel [a-z_0-9]+\(' "$f" && fixtures+=("$f")
done

exported=0; compared=0; rows=0; uncalled=0; errored=0; mismatched=0; failed_fx=0
for f in "${fixtures[@]}"; do
    b=$(basename "$f" .logos)
    o="$TMPD/$b"
    mkdir -p "$o"
    exp="${f%.logos}.expected"
    if ! LOGOS_DEEM_ORACLE="$o" bash "$HERE/run_test.sh" pass "$LOGOSC" "$f" "$exp" > "$o/run.log" 2>&1; then
        echo "FAIL: $b does not pass when compiled for the oracle:"
        sed 's/^/    /' "$o/run.log" | head -8
        failed_fx=$((failed_fx + 1))
        continue
    fi
    for dl in "$o"/*.dl; do
        [ -e "$dl" ] || continue
        q=$(basename "$dl" .dl)
        exported=$((exported + 1))
        if [ -e "$o/$q/deem.err" ] && [ ! -e "$o/$q/deem.out" ]; then errored=$((errored + 1)); continue; fi
        if [ ! -e "$o/$q/deem.out" ]; then uncalled=$((uncalled + 1)); continue; fi
        mkdir -p "$o/$q/souffle"
        if ! "$SOUFFLE" "$dl" -F "$o/$q" -D "$o/$q/souffle" > "$o/$q/souffle.log" 2>&1; then
            echo "FAIL: [$b] $q — souffle rejected the exported program:"
            sed 's/^/    /' "$o/$q/souffle.log" | head -8
            mismatched=$((mismatched + 1))
            continue
        fi
        sort -u "$o/$q/deem.out" > "$o/$q/deem.set"
        sort -u "$o/$q/souffle/__out.csv" > "$o/$q/souffle.set"
        if ! cmp -s "$o/$q/deem.set" "$o/$q/souffle.set"; then
            echo "FAIL: [$b] $q — Deem and Soufflé disagree (< deem, > souffle):"
            diff "$o/$q/deem.set" "$o/$q/souffle.set" | head -12 | sed 's/^/    /'
            mismatched=$((mismatched + 1))
            continue
        fi
        compared=$((compared + 1))
        rows=$((rows + $(wc -l < "$o/$q/deem.set")))
    done
done

echo "souffle oracle: ${#fixtures[@]} fixture(s) with rels; $exported deem(s) exported," \
     "$compared agree with Soufflé ($rows distinct rows), $uncalled never called, $errored returned Err," \
     "$mismatched disagree, $failed_fx fixture(s) failed"
fail=0
[ "$mismatched" -eq 0 ] || fail=1
[ "$failed_fx" -eq 0 ] || fail=1
if [ "$rows" -lt "$ROWS_FLOOR" ]; then
    echo "FAIL: $rows rows compared, floor $ROWS_FLOOR — the comparisons went vacuous"
    fail=1
fi
if [ "$compared" -lt "$COMPARED_FLOOR" ]; then
    echo "FAIL: $compared compared, floor $COMPARED_FLOOR — programs left the shared fragment"
    fail=1
fi
exit $fail  # lint:exit-ok — 0/1 only
