#!/usr/bin/env bash
# rc_seam_gate.sh LOGOSC PASSDIR [--report]
#
# ADR 0025 CRITERION 3'S INSTRUMENT — THE NON-BLIND ONE.
#
# Criterion 3 (Victor, 2026-08-11): «ARC/RC only where needed; in ALL hot
# data-access paths BC alone carries memory safety.» Operationally, on the
# artifact: NO reference-count operation and NO atomic read-modify-write is
# reachable from an emitted query's per-row code. Setup may hold an `Arc` — the
# handle IS `{snap: Arc<dyn Snapshot>, ctr_id}` (req. 2 / §7b) — the criterion is
# about where the counting HAPPENS, not about whether a count exists.
#
# ── WHY THIS SCRIPT EXISTS: THE S5 AUDIT'S FINDING, VERBATIM ────────────────
#
# The instrument S2-S5 used was `grep Arc\|Rc` over an emitted object, and the
# S5 audit showed it was BLIND BY CONSTRUCTION: it was pointed at
# `pass/deem_pipeline_chain`, whose q1 reads a bare `&[Row]` through a
# hand-written iterator. NO program of that shape can contain a reference count,
# whatever the emitter does, so its zero was a fact about the fixture. Measured
# corpus-wide at the time: 77 Arc/Rc symbols, 11 each in exactly 7 fixtures —
# and those 7 were precisely the ones NOT chained. The missing arm the audit
# named: *a pipeline whose q1 reads a container handle*. That arm is
# `pass/deem_pipeline_handle_seam`, and this gate is what reads it.
#
# ── WHAT IS COUNTED, AND WHERE ─────────────────────────────────────────────
#
# `objdump -dr` on the emitted object. Each function is its own `.text.<sym>`
# section, so a relocation is attributable to the function it sits in.
#
#   RC OPERATION  a relocation target matching
#                 `(Arc|Rc)$G<n>$…__(clone_ref|drop|inc|dec)` or `__rc_(inc|dec)`
#                 — the reference-count entry points themselves, not merely a
#                 type name containing "Arc" (a parameter of type `&Arc<T>`
#                 counts nothing, and the old grep could not tell the two apart).
#   ATOMIC RMW    a `lock`-prefixed instruction.
#
#   HOT SET       the transitive call closure, over static relocation edges, from
#                 the emitted query entry points. An entry family is any name `N`
#                 for which `$N_run__f` exists (only a `deem` emits `_run`); the
#                 family is `$N__f`, `$N_prepare__f`, `$N_run__f`, `$N_stream__f`.
#                 `main` is NOT an entry, which is the whole point: the fixture's
#                 own setup drops its `Arc`s and those drops must not be counted
#                 against the emitter.
#
# ⚠ THE CLOSURE CANNOT FOLLOW A `dyn` CALL, and the hot path deliberately makes
#   them (`Arc<dyn Snapshot>` is the store boundary). A closure-only reading
#   would therefore have a hole exactly where the criterion is most interesting.
#   It is closed by a SECOND, dispatch-proof reading of the same object: every
#   RC site in the WHOLE object must fall in the SETUP classifier (`main`,
#   `*__create__*`, `*__open__*`, `*create_ctr*`). If nothing outside setup counts
#   at all, no vtable can route to a count.
#
# ── THE NON-VACUITY ARMS (this is the repair, not decoration) ───────────────
#
# A zero is reported as evidence only when the SAME classifier, on the SAME
# object, demonstrably finds RC operations somewhere:
#
#   V1  the evidence fixture's object must contain >= 1 RC call site overall
#       (measured: 68 — 66 `Arc::drop` in `main`, 2 `Arc::clone_ref` in the
#       family's `__create__`/`__open__`). This is the sensitivity control: the
#       reading "0 in the hot set" is a comparison, not an absence of data.
#   V2  the hot closure must contain a container descent symbol
#       (`__ctr_b*` / `__ctr_leafbatch*`) — i.e. the query really walks the
#       container rather than reading a slice someone handed it.
#   V3  `pass/deem_pipeline_chain` is asserted to be VACUOUS (zero RC symbols in
#       its object). It is kept in the population as the arm the audit refuted,
#       explicitly labelled NOT-EVIDENCE. If it ever stops being vacuous, this
#       gate reds and the reading must be redone rather than inherited.
#
# EXIT 0 pass · 1 a criterion-3 violation · 2 the instrument could not measure
# (missing fixture, compile failure, vacuous evidence arm).
set -uo pipefail
LOGOSC="${1:?logosc}"
PASSDIR="${2:?passdir}"
REPORT="${3:-}"
export LC_ALL=C

EVID=deem_pipeline_handle_seam      # the evidence arm: q1 reads a handle
VAC=deem_pipeline_chain             # the arm the S5 audit refuted

for b in "$EVID" "$VAC"; do
    [ -f "$PASSDIR/$b.logos" ] || { echo "FAIL(2): $PASSDIR/$b.logos missing"; exit 2; }
done

TMPD=$(mktemp -d); trap 'rm -rf "$TMPD"' EXIT
for b in "$EVID" "$VAC"; do
    "$LOGOSC" "$PASSDIR/$b.logos" -o "$TMPD/$b.o" > "$TMPD/$b.out" 2> "$TMPD/$b.err"
    rc=$?
    if [ "$rc" -ne 0 ] || [ ! -s "$TMPD/$b.o" ]; then
        echo "FAIL(2): $b did not compile (rc=$rc) — nothing to measure"
        tail -5 "$TMPD/$b.err"; exit 2
    fi
    objdump -dr "$TMPD/$b.o" > "$TMPD/$b.dis" 2>/dev/null || { echo "FAIL(2): objdump failed on $b"; exit 2; }
done

python3 - "$TMPD/$EVID.dis" "$TMPD/$VAC.dis" "$EVID" "$VAC" "$REPORT" <<'PY'
import re, sys, collections

evid_dis, vac_dis, evid, vac, report = sys.argv[1:6]

# ⚠ NOT `…__(drop)\b`: `_` is a word character, so `\b` never matches inside
# `__drop__g__Arc$G1$T` and the whole classifier silently counted ZERO — caught
# on this fixture, whose object holds 68 sites, by the V1 sensitivity arm below.
RC   = re.compile(r'(Arc|Rc)\$G\d+\$.*__(clone_ref|drop|inc|dec)(__|$)|__rc_(inc|dec)(__|$)')
SETUP= re.compile(r'^main$|__create__|__open__|create_ctr')
WALK = re.compile(r'__ctr_b|__ctr_leafbatch')

def parse(path):
    sec = None
    edges = collections.defaultdict(list)
    locks = collections.Counter()
    funcs = []
    for ln in open(path, errors='replace'):
        m = re.match(r'^Disassembly of section \.text\.(.+):$', ln.rstrip('\n'))
        if m:
            sec = m.group(1); funcs.append(sec); edges[sec]; continue
        if sec is None: continue
        m = re.search(r'\bR_X86_64_\w+\s+(\S+)', ln)
        if m:
            t = re.split(r'[-+]0x', m.group(1))[0]
            edges[sec].append(t)
        if re.search(r'\tlock\b', ln): locks[sec] += 1
    return funcs, edges, locks

def entries(funcs):
    names = set()
    for f in funcs:
        m = re.search(r'\$([A-Za-z_0-9]+)_run__f', f)
        if m: names.add(m.group(1))
    ents = []
    for f in funcs:
        for n in names:
            if re.search(r'\$' + re.escape(n) + r'(_prepare|_run|_stream)?__f', f):
                ents.append(f); break
    return names, ents

def closure(ents, edges):
    seen, stack = set(), list(ents)
    while stack:
        x = stack.pop()
        if x in seen: continue
        seen.add(x)
        stack.extend(edges.get(x, ()))
    return seen

def rc_sites(edges):
    return [(s, t) for s, ts in edges.items() for t in ts if RC.search(t)]

fail = 0

# ── THE EVIDENCE ARM ───────────────────────────────────────────────────────
funcs, edges, locks = parse(evid_dis)
names, ents = entries(funcs)
hot = closure(ents, edges)
sites = rc_sites(edges)
hot_sites = [(s, t) for s, t in sites if s in hot]
out_setup = [(s, t) for s, t in sites if not SETUP.search(s)]
hot_locks = sum(v for k, v in locks.items() if k in hot)
all_locks = sum(locks.values())
walks = sorted(f for f in hot if WALK.search(f))

print(f"[{evid}] queries={len(names)} entries={len(ents)} functions={len(funcs)} hot-closure={len(hot)}")
print(f"[{evid}] RC call sites: object={len(sites)}  hot={len(hot_sites)}  outside-setup={len(out_setup)}")
print(f"[{evid}] lock-prefixed RMW: object={all_locks}  hot={hot_locks}")
print(f"[{evid}] descent symbols in the hot closure: {len(walks)}")
by = collections.Counter(s for s, _ in sites)
for k, v in by.most_common(8):
    print(f"           {v:5d}  {k}")

# V1 sensitivity
if len(sites) < 1:
    print(f"FAIL(2): [{evid}] the object holds NO reference-count call site at all —")
    print( "         the hot-path zero below would be a fact about the fixture, which is")
    print( "         exactly the blindness this instrument replaces.")
    sys.exit(2)
# V2 the query really descends a container
if not walks:
    print(f"FAIL(2): [{evid}] the hot closure contains no container descent symbol —")
    print( "         q1 is not reading a handle, so this is not the arm the audit named.")
    sys.exit(2)
if not names:
    print(f"FAIL(2): [{evid}] no `_run` entry found — no emitted query to measure.")
    sys.exit(2)

# THE CRITERION
if hot_sites:
    print(f"FAIL(1): [{evid}] {len(hot_sites)} reference-count call site(s) in the per-row path:")
    for s, t in hot_sites[:20]: print(f"           {s}\n             -> {t}")
    fail = 1
if out_setup:
    print(f"FAIL(1): [{evid}] {len(out_setup)} reference-count call site(s) outside the SETUP")
    print( "         classifier (this is the dispatch-proof reading — a `dyn` call cannot")
    print( "         hide a count the closure missed):")
    for s, t in out_setup[:20]: print(f"           {s}\n             -> {t}")
    fail = 1
if hot_locks:
    print(f"FAIL(1): [{evid}] {hot_locks} lock-prefixed atomic RMW in the per-row path")
    fail = 1
if all_locks:
    print(f"FAIL(1): [{evid}] {all_locks} lock-prefixed atomic RMW in the object")
    fail = 1

# ── THE ARM THE AUDIT REFUTED — CHECKED IN THE VACUITY DIRECTION ───────────
vfuncs, vedges, vlocks = parse(vac_dis)
vsites = rc_sites(vedges)
vsyms  = [f for f in vfuncs if RC.search(f)]
print(f"[{vac}] NOT-EVIDENCE arm: RC symbols={len(vsyms)} RC call sites={len(vsites)} "
      f"lock RMW={sum(vlocks.values())}")
if vsites or vsyms:
    print(f"FAIL(1): [{vac}] is no longer vacuous — it now holds a reference count.")
    print( "         The S5 audit's reading of it as NOT-EVIDENCE was recorded on the")
    print( "         measurement that it could not hold one. Re-read it, do not inherit.")
    fail = 1

if report:
    print("REPORT ONLY")
    sys.exit(0)
sys.exit(fail)
PY
rc=$?
if [ "$rc" -eq 0 ]; then echo "rc_seam_gate: PASS"; fi
exit "$rc"  # lint:exit-ok — `rc` is python3's wait status, a real byte (0 pass, 1 violation, 2 cannot measure)
