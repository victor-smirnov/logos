#!/usr/bin/env bash
# souffle_oracle_gate.sh LOGOSC PASSDIR SHARD NSHARDS — Soufflé as an INDEPENDENT
# oracle for Deem (Victor 10-03: Soufflé is an architectural oracle).
#
# The population is every `wql_*` / `deem_*` pass fixture, split into NSHARDS
# registered tests (fixture i belongs to shard i mod NSHARDS, in name order):
# lt schedules the shards, so the gate holds no scheduler of its own.
#
# Each fixture is compiled with LOGOS_DEEM_ORACLE: the
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
# wraps, so an error is outside the shared semantics. And an AGGREGATE whose
# dumped inputs hold a duplicate row: Deem folds the rows it scans (a bag),
# Soufflé folds the tuples of a relation (a set), so `count` differs exactly
# there and nowhere else.
#
# ⚠ THE POPULATION IS PINNED FROM BELOW. A change that pushes programs out of
# the fragment would make the gate compare less and stay green; COMPARED_FLOOR
# turns that into a red. Raise it when the population grows.
set -uo pipefail
LOGOSC="$1"
PASS="$2"
SHARD="${3:-0}"
NSHARDS="${4:-1}"
# `SOUFFLE` may be overridden only to bite-check the gate with a perturbing
# wrapper; the oracle is the system Soufflé (2.5, 64-bit word = Deem's i64).
SOUFFLE="${SOUFFLE:-/usr/bin/souffle}"
# per shard (index = SHARD), measured; raise when the population grows
COMPARED_FLOORS=(44 24 20 24)   # 2026-10-03, all 204 wql_/deem_ fixtures: 154 exported, 112 compared
ROWS_FLOORS=(110 66 45 82)
COMPARED_FLOOR="${COMPARED_FLOORS[$SHARD]:-0}"
ROWS_FLOOR="${ROWS_FLOORS[$SHARD]:-0}"
HERE="$(cd "$(dirname "$0")" && pwd)"
DB="$(cd "$(dirname "$LOGOSC")/.." && pwd)/testdb.sqlite"   # lt's test registry, for per-fixture args

if [ ! -x "$SOUFFLE" ]; then
    echo "SKIP: $SOUFFLE not installed — the oracle cannot run"
    exit 77
fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
export LC_ALL=C

fixtures=()
i=0
for f in $(ls "$PASS"/wql_*.logos "$PASS"/deem_*.logos | sort); do
    [ $((i % NSHARDS)) -eq "$SHARD" ] && fixtures+=("$f")
    i=$((i + 1))
done

exported=0; compared=0; rows=0; bag=0; uncalled=0; errored=0; mismatched=0; failed_fx=0
for f in "${fixtures[@]}"; do
    b=$(basename "$f" .logos)
    o="$TMPD/$b"
    mkdir -p "$o"
    exp="${f%.logos}.expected"
    # the fixture's registered extra arguments (a module search path, …), as lt
    # runs it: everything after run_test.sh's four positional arguments
    extra=()
    if [ -f "$DB" ]; then
        mapfile -t extra < <(sqlite3 "$DB" "select command from tests where name='logos_02_semantic_core_pass_$b'" \
            | python3 -c 'import json,sys; t=sys.stdin.read().strip(); [print(a) for a in (json.loads(t)[5:] if t else [])]')
    fi
    if ! LOGOS_DEEM_ORACLE="$o" bash "$HERE/run_test.sh" pass "$LOGOSC" "$f" "$exp" "${extra[@]}" > "$o/run.log" 2>&1; then
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
        if grep -q ' : { ' "$dl"; then
            dup=0
            for ff in "$o/$q"/*.facts; do
                [ -e "$ff" ] || continue
                [ -n "$(sort "$ff" | uniq -d | head -1)" ] && dup=1
            done
            if [ "$dup" -eq 1 ]; then bag=$((bag + 1)); continue; fi
        fi
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

echo "souffle oracle shard $SHARD/$NSHARDS: ${#fixtures[@]} fixture(s); $exported deem(s) exported," \
     "$compared agree with Soufflé ($rows distinct rows), $uncalled never called, $errored returned Err, $bag aggregate(s) over a bag input," \
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
