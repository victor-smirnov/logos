#!/usr/bin/env bash
# souffle_oracle_gate.sh LOGOSC PASSDIR SHARD NSHARDS — Soufflé as an INDEPENDENT
# oracle for Deem (Victor 10-03: Soufflé is an architectural oracle).
#
# The population is every `wql_*` / `deem_*` pass fixture, split into NSHARDS
# registered tests (a fixture belongs to shard cksum(name) mod NSHARDS):
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
# Not compared, and counted: a deem outside the fragment (dl_export writes the
# first construct it refused to `<q>.skip`; the gate prints the reasons), a
# deem the program never called (no deem.out), and
# a call that returned Err (deem.err): Deem's arithmetic is checked, Soufflé's
# wraps, so an error is outside the shared semantics. And an AGGREGATE whose
# dumped inputs hold a duplicate row: Deem folds the rows it scans (a bag),
# Soufflé folds the tuples of a relation (a set), so `count` differs exactly
# there and nowhere else.
#
# `first` / `limit N|p`: Soufflé has no order, so the exported clause is the
# UNLIMITED query (the .dl carries a `// limit` marker) and Deem's rows must be
# a subset of its answer, at most n of them, and all of it when fewer than n.
#
# An f64 column is Soufflé's `float`: Deem dumps it as C99 hex (exact), the
# gate compares float VALUES (-0 equal to +0, one NaN). A NaN input is left out
# only for a program that orders floats (`// nan-sensitive`: a float min/max).
#
# THE INCREMENTAL TIER. A deem with an `_epoch`/`_retract`/`_snapshot` handle
# also logs, per handle (`<q>/h<id>.log`), every Ok call of the fn that changes
# it (`+ row`, `- row`; a weighted `_apply` logs |w| copies) and every snapshot
# (`= k`, rows in `h<id>.s<k>`). The gate replays the log and runs Soufflé over
# the input as it stood at each snapshot (at most 25 per deem). The replay's
# algebra is the tier's: a rel-backed handle keeps a SET (a second insert is a
# no-op), an aggregate handle a Z-set — and a snapshot taken over a duplicate is
# skipped and counted, for the batch path's reason. An Ok retraction that drives
# a count below zero is a disagreement in itself.
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
COMPARED_FLOORS=(131 54 47 87)   # 2026-10-04, + float literals: 319 compared in all
ROWS_FLOORS=(2130 175 119 246)
LIMITED_FLOORS=(6 5 9 4)      # first/limit checked as subsets, 24 in all
ISNAP_FLOORS=(102 29 35 59)    # incremental snapshots, 225 in all
COMPARED_FLOOR="${COMPARED_FLOORS[$SHARD]:-0}"
ISNAP_FLOOR="${ISNAP_FLOORS[$SHARD]:-0}"
ROWS_FLOOR="${ROWS_FLOORS[$SHARD]:-0}"
LIMITED_FLOOR="${LIMITED_FLOORS[$SHARD]:-0}"
# ── KNOWN INCREMENTAL DISAGREEMENTS, checked BOTH WAYS ──────────────────────
# `fixture:deem` pairs whose handle disagrees with Soufflé because the fixture
# PINS a defect on purpose. An entry whose pair agrees (the defect was fixed)
# or never logs (the fixture moved) is a red, so the ledger cannot outlive its
# reason.
# Empty since 2026-10-03: the three wql_incr_retract_footprint_identity pairs
# it held (footprint identity, `count`'s empty footprint, non-atomic `_apply`)
# agree now that the sensor matches whole rows and `_apply` commits on Ok.
declare -A KNOWN_INC=()
declare -A SEEN_INC=()
HERE="$(cd "$(dirname "$0")" && pwd)"
DB="$(cd "$(dirname "$LOGOSC")/.." && pwd)/testdb.sqlite"   # lt's test registry, for per-fixture args

if [ ! -x "$SOUFFLE" ]; then
    echo "SKIP: $SOUFFLE not installed — the oracle cannot run"
    exit 77
fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
# rewrite the `float` columns of answer files to one spelling of their VALUE
cat > "$TMPD/_fnorm.py" <<'FNORM'
import sys
dl = sys.argv[1]
d = [l for l in open(dl).read().splitlines() if l.startswith('.decl __out(')][0]
fc = [i for i, c in enumerate(d[len('.decl __out('):-1].split(', ')) if c.endswith(': float')]
def fn(v):
    x = float.fromhex(v) if '0x' in v else float(v)
    return 'nan' if x != x else (0.0).hex() if x == 0 else x.hex()
for f in sys.argv[2:]:
    out = []
    for line in open(f).read().splitlines():
        c = line.split('\t')
        for i in fc:
            if i < len(c): c[i] = fn(c[i])
        out.append('\t'.join(c))
    open(f, 'w').write(''.join(l + '\n' for l in out))
FNORM
export LC_ALL=C

# Membership is a hash of the fixture's NAME, not its position: with "index mod
# N" one new fixture moved every later one to another shard and every floor
# drifted (measured: shard 2 lost 7 comparisons to a fixture added elsewhere).
fixtures=()
for f in "$PASS"/wql_*.logos "$PASS"/deem_*.logos; do
    h=$(basename "$f" .logos | cksum | cut -d' ' -f1)
    [ $((h % NSHARDS)) -eq "$SHARD" ] && fixtures+=("$f")
done

exported=0; skipped=0; compared=0; limited=0; nanin=0; rows=0; bag=0; isnap=0; imis=0; ibag=0; iknown=0; uncalled=0; errored=0; mismatched=0; failed_fx=0
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
    # deems outside the fragment: dl_export names the reason in `<q>.skip`
    for sk in "$o"/*.skip; do
        [ -e "$sk" ] || continue
        skipped=$((skipped + 1))
        printf '%s\t%s\n' "$(head -1 "$sk")" "$b:$(basename "$sk" .skip)" >> "$TMPD/_skips"
    done
    for dl in "$o"/*.dl; do
        [ -e "$dl" ] || continue
        q=$(basename "$dl" .dl)
        exported=$((exported + 1))
        # THE INCREMENTAL TIER (see the header)
        if ls "$o/$q"/h*.log > /dev/null 2>&1; then
            res=$(python3 - "$dl" "$o/$q" "$SOUFFLE" 2> "$o/$q/inc.log" <<'PY'
import glob, os, subprocess, sys, tempfile
dl, d, souffle = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(dl).read().splitlines()
inputs = [l.split()[1] for l in text if l.startswith('.input ') and not l.split()[1].startswith('__p_')]
if len(inputs) != 1:
    print('0 0 0'); sys.exit(0)
src = inputs[0]
decls = [l.split()[1].split('(')[0] for l in text if l.startswith('.decl ')]
# THE TIER DECIDES THE INPUT'S ALGEBRA. A rel-backed handle presence-gates its
# input (a set: a second insert is a no-op); an aggregate-only handle folds a
# Z-set (a duplicate row counts twice), exactly as the batch fn folds a bag.
relbacked = any(not (x == src or x.startswith('__')) for x in decls)
# a `float` output column is written as C99 hex by Deem and as %.17g by
# Soufflé: compare VALUES (-0 equal to +0, one NaN)
outd = [l for l in text if l.startswith('.decl __out(')]
fcols = [i for i, c in enumerate(outd[0][len('.decl __out('):-1].split(', ')) if c.endswith(': float')] if outd else []
def fnorm(v):
    x = float.fromhex(v) if '0x' in v else float(v)
    return 'nan' if x != x else (0.0).hex() if x == 0 else x.hex()
# the INPUT's float columns, canonical as the handle's identity is (-0 is +0,
# one NaN), so a retraction of +0 matches an insert of -0 here too
ind = [l for l in text if l.startswith('.decl ' + src + '(')]
icols = [i for i, c in enumerate(ind[0][len('.decl ' + src + '('):-1].split(', ')) if c.endswith(': float')] if ind else []
nansens = any(l.startswith('// nan-sensitive') for l in text)
def inorm(row):
    if not icols: return row
    c = row.split('\t')
    for i in icols:
        if i < len(c): c[i] = fnorm(c[i])
    return '\t'.join(c)
def norm(row):
    if not fcols: return row
    c = row.split('\t')
    for i in fcols:
        if i < len(c): c[i] = fnorm(c[i])
    return '\t'.join(c)
agg = ' : { ' in open(dl).read()
n = mis = skip = 0
for log in sorted(glob.glob(d + '/h*.log')):
    cnt = {}
    stem = log[:-4]
    lines = open(log).read().splitlines()
    if '?' in lines:   # a delta on a second source: not one input's history
        skip += sum(1 for l in lines if l.startswith('=')); continue
    for line in lines:
        if not line: continue
        op, rest = line[0], line[2:]
        if op in '+-': rest = inorm(rest)
        if op == '+':
            cnt[rest] = 1 if relbacked else cnt.get(rest, 0) + 1
        elif op == '-':
            cnt[rest] = 0 if relbacked else cnt.get(rest, 0) - 1
        elif op == '=' and n + skip < 25:
            snap = stem + '.s' + rest
            neg = [r for r, c in cnt.items() if c < 0]
            if neg:
                n += 1; mis += 1
                print('MISMATCH %s %s: an Ok retraction of rows the handle never held %s' % (os.path.basename(log), rest, neg[:3]), file=sys.stderr)
                continue
            if agg and any(c > 1 for c in cnt.values()):
                skip += 1; continue
            if nansens and any('nan' in r.split('\t') for r, c in cnt.items() if c > 0):
                skip += 1; continue
            rows = sorted(r for r, c in cnt.items() if c > 0)
            t = tempfile.mkdtemp()
            with open(t + '/' + src + '.facts', 'w') as f:
                f.write(''.join(r + '\n' for r in rows))
            os.mkdir(t + '/out')
            p = subprocess.run([souffle, dl, '-F', t, '-D', t + '/out'], capture_output=True)
            got = set(map(norm, open(t + '/out/__out.csv').read().splitlines())) if p.returncode == 0 and os.path.exists(t + '/out/__out.csv') else None
            want = set(map(norm, open(snap).read().splitlines())) if os.path.exists(snap) else None
            n += 1
            if got is None or want is None or got != want:
                mis += 1
                print('MISMATCH %s %s: deem-only %s souffle-only %s' % (os.path.basename(log), rest,
                      sorted((want or set()) - (got or set()))[:3], sorted((got or set()) - (want or set()))[:3]), file=sys.stderr)
print('%d %d %d' % (n, mis, skip))
PY
            )
            read -r rn rm rs <<< "$res"
            isnap=$((isnap + ${rn:-0})); ibag=$((ibag + ${rs:-0}))
            if [ -n "${KNOWN_INC[$b:$q]:-}" ]; then
                SEEN_INC[$b:$q]=1
                if [ "${rm:-1}" = "0" ]; then
                    echo "FAIL: [$b] $q — listed in KNOWN_INC but agrees with Soufflé: remove the entry"
                    imis=$((imis + 1))
                else
                    iknown=$((iknown + 1))
                fi
            elif [ "${rm:-1}" != "0" ]; then
                echo "FAIL: [$b] $q — an incremental snapshot disagrees with Soufflé over the replayed input:"
                head -4 "$o/$q/inc.log" | sed 's/^/    /'
                imis=$((imis + ${rm:-1}))
            fi
        fi
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
        # a NaN in a float input of a program that ORDERS floats (a float
        # min/max, marked by dl_export): Deem's total order puts it greatest,
        # Soufflé's min/max skips it. Comparisons are IEEE in both.
        if grep -q '^// nan-sensitive' "$dl" && grep -qE '(^|	)-?nan(	|$)' "$o/$q"/*.facts 2>/dev/null; then nanin=$((nanin + 1)); continue; fi
        mkdir -p "$o/$q/souffle"
        if ! "$SOUFFLE" "$dl" -F "$o/$q" -D "$o/$q/souffle" > "$o/$q/souffle.log" 2>&1; then
            echo "FAIL: [$b] $q — souffle rejected the exported program:"
            sed 's/^/    /' "$o/$q/souffle.log" | head -8
            mismatched=$((mismatched + 1))
            continue
        fi
        if grep -q '^\.decl __out(.*: float' "$dl"; then
            # float columns: Deem writes C99 hex, Soufflé %.17g — compare values
            python3 "$TMPD/_fnorm.py" "$dl" "$o/$q/deem.out" "$o/$q/souffle/__out.csv"
        fi
        sort -u "$o/$q/deem.out" > "$o/$q/deem.set"
        sort -u "$o/$q/souffle/__out.csv" > "$o/$q/souffle.set"
        # `first` / `limit N|p`: the .dl is the UNLIMITED query (Soufflé has no
        # order), so Deem's rows must be a subset of it, at most n of them, and
        # all of it when there are fewer than n
        lim=$(sed -n 's|^// first$|1|p; s|^// limit \([0-9][0-9]*\)$|\1|p' "$dl")
        lp=$(sed -n 's|^// limit param \(.*\)$|\1|p' "$dl")
        [ -n "$lp" ] && lim=$(head -1 "$o/$q/__p_$lp.facts" 2>/dev/null)
        if [ -n "$lim" ]; then
            nd=$(wc -l < "$o/$q/deem.out")
            extra=$(comm -23 "$o/$q/deem.set" "$o/$q/souffle.set" | head -3)
            if [ -n "$extra" ] || [ "$nd" -gt "$lim" ] || { [ "$nd" -lt "$lim" ] && ! cmp -s "$o/$q/deem.set" "$o/$q/souffle.set"; }; then
                echo "FAIL: [$b] $q — limit $lim: Deem's $nd row(s) are not a subset of Soufflé's answer of that size:"
                diff "$o/$q/deem.set" "$o/$q/souffle.set" | head -12 | sed 's/^/    /'
                mismatched=$((mismatched + 1))
                continue
            fi
            limited=$((limited + 1))
            rows=$((rows + $(wc -l < "$o/$q/deem.set")))
            continue
        fi
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

for k in "${!KNOWN_INC[@]}"; do
    h=$(printf '%s\n' "${k%%:*}" | cksum | cut -d' ' -f1)   # as the fixture loop hashes it
    [ $((h % NSHARDS)) -eq "$SHARD" ] || continue
    if [ -z "${SEEN_INC[$k]:-}" ]; then
        echo "FAIL: KNOWN_INC entry $k logged no snapshot: remove or fix the entry"
        imis=$((imis + 1))
    fi
done
# what the oracle cannot see, by the first construct dl_export refused
if [ -s "$TMPD/_skips" ]; then
    echo "outside the fragment, by reason (deems; an example):"
    cut -f1 "$TMPD/_skips" | sort | uniq -c | sort -rn | while read -r c r; do
        printf '  %4d  %s  (%s)\n' "$c" "$r" "$(grep -F -m1 "$r	" "$TMPD/_skips" | cut -f2)"
    done
fi
echo "souffle oracle shard $SHARD/$NSHARDS: ${#fixtures[@]} fixture(s); $exported deem(s) exported, $skipped outside the fragment," \
     "$compared agree with Soufflé, $limited first/limit within it ($rows distinct rows), $uncalled never called, $errored returned Err, $bag aggregate(s) over a bag input, $nanin with a NaN input," \
     "$mismatched disagree, $failed_fx fixture(s) failed;" \
     "incremental: $isnap snapshot(s) compared, $ibag over a bag skipped, $iknown known (KNOWN_INC), $imis disagree"
fail=0
[ "$mismatched" -eq 0 ] || fail=1
[ "$imis" -eq 0 ] || fail=1
[ "$failed_fx" -eq 0 ] || fail=1
if [ "$rows" -lt "$ROWS_FLOOR" ]; then
    echo "FAIL: $rows rows compared, floor $ROWS_FLOOR — the comparisons went vacuous"
    fail=1
fi
if [ "$limited" -lt "$LIMITED_FLOOR" ]; then
    echo "FAIL: $limited first/limit deem(s) checked, floor $LIMITED_FLOOR — programs left the fragment"
    fail=1
fi
if [ "$isnap" -lt "$ISNAP_FLOOR" ]; then
    echo "FAIL: $isnap incremental snapshot(s) compared, floor $ISNAP_FLOOR — handles stopped logging"
    fail=1
fi
if [ "$compared" -lt "$COMPARED_FLOOR" ]; then
    echo "FAIL: $compared compared, floor $COMPARED_FLOOR — programs left the shared fragment"
    fail=1
fi
exit $fail  # lint:exit-ok — 0/1 only
