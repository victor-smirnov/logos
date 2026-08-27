#!/usr/bin/env bash
# pass-probe.sh <probe-name> — which PROGRAMS THAT COMPILE does this mechanism
# change the verdict of, and which of those changes are FINDINGS vs COST?
#
# The sibling of scripts/ceiling-probe.sh, and it exists because that one
# cannot be pointed at this population. See include/logos/compiler/probe.hpp
# for what a ceiling probe is.
#
# ── WHY A SECOND READER ─────────────────────────────────────────────────────
# ceiling-probe.sh works by an accident of shape: every `bc_admits.ledger` row
# is a registered ctest test ASSERTING THE DEFECT IS STILL HERE, so a row the
# probe closes is a FAILING test and ctest names it. Nothing had to be built.
# MEASURED 2026-08-27: `genrecvtie`, a probe for the generic-autoref hole,
# fired ONCE across 423 ledger compiles and scored ceiling 0 — while its
# insurance twin fired 176 555 times, proving the site hot. The defect's
# population is the STDLIB AND THE PASS CORPUS, not the acceptance ledger, and
# no tool in the tree could price it. That is what this is.
#
# ── ⚠ THE POLARITY IS INVERTED, AND THAT IS THE WHOLE DESIGN PROBLEM ────────
# In the ledger, a probe-induced FAILURE is good news. Among programs that
# COMPILE there is no such assertion. An armed probe makes some passing tests
# fail, and those failures MIX two opposite things:
#     (i)  programs WRONGLY ADMITTED, now correctly refused  — the FINDING
#     (ii) LEGAL programs now wrongly refused                — the COST
# ceiling-probe.sh never had to separate these. This does, and it cannot do it
# by itself: nothing in a compile's exit code says which one it is. What it
# does instead is RANK BY WHOSE WORD YOU WOULD BE TAKING, using the assertions
# the tree already carries:
#
#   population                      what it asserts               a change is
#   ------------------------------  ----------------------------  ------------
#   tests/imported/admit (ledger)   rustc REJECTS it, we admit    FINDING
#   tests/imported/pass             rustc COMPILES it             COST (rustc)
#   stdlib build                    it must build, or nothing     COST (hard)
#   tests/logos/pass                a fixture author said legal   COST (ours)
#   tests/{logos,imported}/fail     an EXACT diagnostic, pinned   READ IT
#
# ⚠ THE SORT IS NOT AUTOMATIC AND THIS SCRIPT DOES NOT PRETEND IT IS. A
# tests/logos/pass fixture that is ITSELF a wrong admission looks exactly like
# cost here; only rustc's column is an external oracle. The `fail` bucket is
# handed back UNSORTED with the path of each program, because a changed
# diagnostic can mean the probe refused the program for a NEW and WRONG reason
# on top of the old right one. Reading those is the price; there are usually
# few of them, and knowing WHICH few is the saving.
#
# ── A ZERO IS NOT AN ANSWER UNTIL THE SITE IS PROVEN LIVE ───────────────────
# Same rule as ceiling-probe.sh, same reason, and it caught a real bug there on
# the first run. `on()` counts arrivals; a run with no fires is reported NEVER
# FIRED, not "no changes".
#
#   $ scripts/pass-probe.sh genautoref            # full population
#   $ scripts/pass-probe.sh genautoref --fast     # skip imported (loses rustc)
#   $ scripts/pass-probe.sh --selftest            # the reader's known answers
#
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
NAME="${1:?usage: pass-probe.sh <probe-name> [--fast] | --selftest}"
FAST=0; [ "${2:-}" = "--fast" ] && FAST=1
JOBS=$(nproc)
BIN=build/bin/logosc
WORK=build/probe
mkdir -p "$WORK"

# ── THE KNOWN ANSWERS. BOTH POLES, BOTH RE-RUNNABLE ─────────────────────────
# A reader that has never SEEN a change cannot tell a dead hypothesis from a
# broken reader — that exact bug shipped in ceiling-probe.sh's first run (a
# relative fire-log path, opened from ctest's working directory) and only its
# known answer caught it. One pole is not enough either: a reader that reports
# "everything changed" unconditionally would pass a refuse-everything selftest
# and be worthless. So: selftest_refuse MUST change nearly everything, and
# selftest_inert — a pure observer at the SAME site, so proven to be reached —
# MUST change nothing.
if [ "$NAME" = "--selftest" ]; then
    fails=0
    echo "pass-probe selftest: pole 1/2 — selftest_refuse must break the corpus"
    # ⚠ INTO A FILE, THEN MATCH IT. `printf … | grep -q` under `set -o
    # pipefail` reports the MATCH as a failed pipeline: grep exits at the first
    # hit and the writer takes SIGPIPE 141. gate_lint's R2 caught this here,
    # and it is the same class as the recorded "pipefail+grep -q" gate lie.
    o1=$("$0" selftest_refuse --fast 2>&1); printf '%s\n' "$o1" > "$WORK/selftest-pole1.txt"
    c1=$(printf '%s' "$o1" | grep -oP 'CHANGED\s+=\s+\K\d+')
    if [ "${c1:-0}" -lt 100 ]; then
        echo "FAIL pass-probe: selftest_refuse changed only ${c1:-none} programs."
        echo "     Refusing every borrow must break hundreds. The READER is broken."
        printf '%s\n' "$o1" | head -20; fails=1
    elif ! grep -q 'COST  — ' "$WORK/selftest-pole1.txt"; then
        echo "FAIL pass-probe: selftest_refuse changed $c1 programs but the SORT printed"
        echo "     no bucket. The counter and the classifier are two instruments and"
        echo "     only one of them was ever checked — that is how the stdin-eating"
        echo "     heredoc survived the first selftest."
        printf '%s\n' "$o1" | head -20; fails=1
    else
        echo "ok   selftest_refuse changed $c1 programs and the sort named buckets"
    fi
    echo "pass-probe selftest: pole 2/2 — selftest_inert must change nothing"
    o2=$("$0" selftest_inert --fast 2>&1)
    c2=$(printf '%s' "$o2" | grep -oP 'CHANGED\s+=\s+\K\d+')
    f2=$(printf '%s' "$o2" | grep -oP 'fired \K\d+')
    if [ "${f2:-0}" -lt 1 ]; then
        echo "FAIL pass-probe: selftest_inert NEVER FIRED — the null pole proves nothing."
        printf '%s\n' "$o2" | head -20; fails=1
    elif [ "${c2:-1}" -ne 0 ]; then
        echo "FAIL pass-probe: selftest_inert changed ${c2} programs while changing NO"
        echo "     behaviour. The reader is inventing changes (flaky tests, dirty"
        echo "     baseline, or a stale binary)."
        printf '%s\n' "$o2" | head -20; fails=1
    else
        echo "ok   selftest_inert fired $f2 times and changed 0 — no invented changes"
    fi
    [ "$fails" -eq 0 ] && { echo "ok  pass-probe: both poles"; exit 0; }
    exit 1
fi

[ -x "$BIN" ] || { echo "pass-probe: no $BIN — build first"; exit 2; }
# ⚠ FRESHNESS IS A PROPERTY OF THE BUILD, not of the checkout: a control and a
# non-control are indistinguishable when neither was compiled.
NEWER=$(find src include -newer "$BIN" \( -name '*.cpp' -o -name '*.hpp' \) 2>/dev/null | head -1)
[ -n "$NEWER" ] && { echo "pass-probe: $BIN is OLDER than $NEWER — rebuild"; exit 2; }

KEY=$(sha256sum "$BIN" | cut -c1-16)
if [ "$FAST" = 1 ]; then
    SEL=(-L '^(pass|fail)$' -LE imported); TAG=fast
else
    SEL=(-L '^(pass|fail)$'); TAG=full
fi
NTESTS=$(ctest --test-dir build -N "${SEL[@]}" 2>/dev/null | grep -oP 'Total Tests: \K\d+')

run_pop() {  # $1=outfile ; env already set
    ctest --test-dir build -j"$JOBS" "${SEL[@]}" 2>/dev/null \
        | grep -oP '^\s*\d+ - \K\S+(?= \(Failed\))' | sort > "$1"
}

# The baseline is a property of the BINARY: key it on the binary hash and pay
# once per build, not once per probe. Batching N probes into one build is the
# point; re-measuring the same baseline N times throws it away.
BASE="$WORK/pass-baseline-$TAG-$KEY.txt"
if [ ! -f "$BASE" ]; then
    echo "pass-probe: baseline for this binary not yet measured ($NTESTS tests, ${JOBS}-way)"
    run_pop "$BASE"
fi
NBASE=$(wc -l < "$BASE")
if [ "$NBASE" -ne 0 ]; then
    echo "pass-probe: ⚠ BASELINE IS NOT CLEAN — $NBASE of $NTESTS already fail with no probe."
    echo "pass-probe:   Selecting by LABEL breaks FIXTURES_REQUIRED, so some of these are"
    echo "pass-probe:   tests that never ran their producer. Every number below is a"
    echo "pass-probe:   DELTA against that set, so those cancel — but they are not green."
fi

# ⚠ ABSOLUTE PATH. Each test is its own logosc process, run by ctest from a
# working directory that is NOT this script's; a relative fire log is opened
# somewhere else, or nowhere, and the harness then reports NEVER FIRED for a
# probe that fired hundreds of times. That is not hypothetical — it is what
# happened on ceiling-probe.sh's first run.
FIRE=$ROOT/$WORK/passfire-$NAME.$$
: > "$FIRE"
RUN=$WORK/passrun-$NAME-$TAG.txt
LOGOS_PROBE="$NAME" LOGOS_PROBE_FIRE="$FIRE" run_pop "$RUN"

# ── THE STDLIB IS A POPULATION MEMBER, AND THE LOUDEST ONE ──────────────────
# It asserts legality by BEING BUILT — no fixture author's opinion involved —
# and the generic-autoref census put most of its arrivals here. It is also the
# one member ctest cannot report, so it is measured separately.
STDLIB_RC=0
if [ "${SKIP_STDLIB:-0}" != "1" ]; then
    STAMP=$(ls -l --time-style=+%s build/lib/logos/liblogos-std.a 2>/dev/null | awk '{print $6}')
    rm -f build/lib/logos/liblogos-{lang,lcm,mem,std}.a
    LOGOS_PROBE="$NAME" LOGOS_PROBE_FIRE="$FIRE" \
        ninja -C build lib/logos/liblogos-lang.a lib/logos/liblogos-lcm.a \
                       lib/logos/liblogos-mem.a  lib/logos/liblogos-std.a \
        > "$WORK/stdlib-$NAME.log" 2>&1
    STDLIB_RC=$?
    # Restore an unprobed stdlib: leaving a probe-built (or half-built) archive
    # behind would poison every later measurement on this tree.
    ninja -C build lib/logos/liblogos-lang.a lib/logos/liblogos-lcm.a \
                   lib/logos/liblogos-mem.a  lib/logos/liblogos-std.a \
        > /dev/null 2>&1 || true
    [ -n "${STAMP:-}" ] || true
fi

FIRES=$(awk -F'\t' -v n="$NAME" '$1==n {s+=$2} END{print s+0}' "$FIRE")
PROCS=$(awk -F'\t' -v n="$NAME" '$1==n' "$FIRE" | wc -l)
rm -f "$FIRE"

if [ "$FIRES" -eq 0 ]; then
    echo "pass-probe: ✗ '$NAME' NEVER FIRED — not reached in any of the $NTESTS compiles"
    echo "pass-probe:   nor in the stdlib build. This is NOT 'no changes'. Either the"
    echo "pass-probe:   name is mis-typed, a guard upstream prunes the site, or the path"
    echo "pass-probe:   is dead. Prove the site is live before reading any zero off it."
    exit 3
fi

CHANGED=$(comm -13 "$BASE" "$RUN")
N=$(printf '%s' "$CHANGED" | grep -c . || true)
UNBROKE=$(comm -23 "$BASE" "$RUN" | grep -c . || true)

echo "pass-probe: '$NAME' fired $FIRES times in $PROCS of $NTESTS+stdlib compiles"
echo "pass-probe: CHANGED = $N programs ($TAG population: $NTESTS ctest + stdlib)"
[ "$UNBROKE" -ne 0 ] && echo "pass-probe: ⚠ $UNBROKE previously-failing tests now pass — the probe UN-refused something"

# ── THE SORT ────────────────────────────────────────────────────────────────
# A ctest name does not carry its source directory, so map name -> file by
# stem. Ambiguity is REPORTED, never resolved by guessing: two files with the
# same stem in different tiers would be exactly the case where the tier is the
# whole answer.
# ⚠ THE LIST GOES IN BY FILE, NOT BY PIPE. Measured the hard way on this
# script's first real run: `printf ... | python3 - <<'PY'` reports CHANGED = 7
# and then prints "nothing changed", because the heredoc IS stdin and the pipe
# is discarded. The selftest passed anyway — it read the count, which was
# right, and never looked at the sort. It now checks both.
CHFILE=$WORK/passchanged-$NAME-$TAG.txt
printf '%s\n' "$CHANGED" | grep . > "$CHFILE"
python3 - "$ROOT" "$CHFILE" <<'PY'
import os, sys, collections
root = sys.argv[1]
names = [l.strip() for l in open(sys.argv[2]) if l.strip()]
if not names:
    print("pass-probe: nothing changed."); sys.exit(0)
idx = collections.defaultdict(list)
for base in ("tests/logos", "tests/imported"):
    for dp, _, fs in os.walk(os.path.join(root, base)):
        for f in fs:
            if f.endswith(".logos"):
                idx[f[:-6]].append(os.path.relpath(os.path.join(dp, f), root))
def where(name):
    parts = name.split("_")
    for i in range(len(parts)):
        stem = "_".join(parts[i:])
        if stem in idx:
            return idx[stem]
    return []
BUCKETS = [
 ("FINDING      (rustc REJECTS it; we admitted it; the probe now refuses it)",
  lambda p: "/imported/admit/" in "/"+p),
 ("COST  — rustc-attested (rustc COMPILES it; the probe refuses it: wrong)",
  lambda p: "/imported/pass/" in "/"+p),
 ("COST  — ours (a tests/logos/pass fixture asserts legality; author's word)",
  lambda p: "tests/logos/pass/" in p),
 ("READ IT — a pinned diagnostic changed; refused before AND after, for what",
  lambda p: "/fail/" in "/"+p),
]
seen, out = set(), collections.defaultdict(list)
unknown = []
for n in names:
    ps = where(n)
    if not ps:
        unknown.append(n); continue
    placed = False
    for label, pred in BUCKETS:
        hit = [p for p in ps if pred(p)]
        if hit:
            out[label].append((n, hit)); placed = True; break
    if not placed:
        unknown.append(n + "  -> " + ",".join(ps))
for label, _ in BUCKETS:
    rows = out.get(label, [])
    if not rows: continue
    print(f"pass-probe: [{len(rows)}] {label}")
    for n, ps in rows[:25]:
        print(f"pass-probe:     {n}\n pass-probe:        {' '.join(ps)}")
    if len(rows) > 25: print(f"pass-probe:     ... and {len(rows)-25} more")
if unknown:
    print(f"pass-probe: [{len(unknown)}] UNSORTED — no .logos found for the name, or an")
    print( "pass-probe:      ambiguous stem. These are handed back RAW on purpose.")
    for n in unknown[:25]: print("pass-probe:     " + n)
PY

if [ "$STDLIB_RC" -ne 0 ]; then
    echo "pass-probe: ⛔ THE STDLIB DID NOT BUILD under this probe — the hardest COST"
    echo "pass-probe:    there is. It asserts legality by existing; nothing downstream"
    echo "pass-probe:    of it is meaningful. First refusals ($WORK/stdlib-$NAME.log):"
    grep -m4 -E 'error|refus|cannot' "$WORK/stdlib-$NAME.log" | sed 's/^/pass-probe:      /'
elif [ "${SKIP_STDLIB:-0}" != "1" ]; then
    echo "pass-probe: stdlib built clean under the probe"
fi

cat <<'LIMIT'
pass-probe: ── WHAT THIS CANNOT SEE ────────────────────────────────────────
pass-probe: ⚠ 0 CHANGED IS NOT A SAFETY CLAIM, AND 0 COST NEVER WAS. A corpus
pass-probe:   refuses only programs it CONTAINS. Measured twice on consecutive
pass-probe:   days, both times the counter-example had to be CONSTRUCTED — 18
pass-probe:   hand-written legal programs found the last one. This removes bad
pass-probe:   candidates cheaply; it never certifies a good one.
pass-probe: ⚠ THE FINDING/COST SORT IS BY ORACLE, NOT BY TRUTH. Only the rustc
pass-probe:   column is external. A tests/logos/pass fixture that is itself a
pass-probe:   wrong admission is indistinguishable here from real cost — which
pass-probe:   is the very defect class this tool was built to hunt.
pass-probe: ⚠ IT PRICES A PROBE, NOT A FIX. The probe is deliberately crude; a
pass-probe:   correct mechanism with exemptions will cost less and may find
pass-probe:   less. This is an UPPER bound on both columns.
pass-probe: ⚠ A PROGRAM IS THE UNIT, NOT A BORROW. One changed test can be one
pass-probe:   wrong refusal or forty; the fire count is the only arrival count.
LIMIT
exit 0
