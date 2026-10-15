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
#   RC OPERATION  a call to a COUNTING function: one whose body writes a count
#                 (ArcInner/RcBox `strong`/`weak`, a Memoria node's rc header)
#                 or calls one that does — DERIVED from stdlib/mem + stdlib/lang
#                 source, never a list of spellings (ADR 0025 L5: the old regex
#                 `__(clone_ref|drop|inc|dec)` saw 68 of 113 sites, missing
#                 `clone_arc`, `downgrade`, `upgrade`, `Weak`, `inc_rc`, …).
#   ATOMIC RMW    a call to an ATOMIC function, derived the same way from the
#                 RMW externs of `logos.lang.atomic` (ADR 0025 L6: a `lock`
#                 instruction never appears in an emitted object — every atomic
#                 is a call into the library — so the old scan could not fire).
#   A call made INSIDE a counting function is that function's body, charged to
#   its caller and not counted twice.
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
#   It is closed by a SECOND, dispatch-proof reading of the same object: an RC
#   or atomic site outside the SETUP classifier (`main`, `*__create__*`,
#   `*__open__*`, `*create_ctr*`) is admitted only when no function that
#   reaches it has an ESCAPING address (a non-call text relocation, or any data
#   relocation) — then no indirect call, from anywhere, can enter it. Measured:
#   15 such sites, all the container's write plane (`insert`, `remove`,
#   `with_entry`: Memoria's copy-on-write block counts), none reachable.

# ── THE NON-VACUITY ARMS (this is the repair, not decoration) ───────────────
#
# A zero is reported as evidence only when the SAME classifier, on the SAME
# object, demonstrably finds RC operations somewhere:
#
#   V1  the evidence fixture's object must contain >= 1 RC and >= 1 atomic call
#       site overall (measured: 113 RC, 95 atomic, almost all `Arc::drop` in
#       `main`). The derivation itself carries canaries (`Arc::clone_arc`,
#       `Rc::clone_rc`, `Weak::upgrade`, `node_inc_rc`, `MemBlockPtr::inc_rc`,
#       atomic `Arc::drop_arc`): a seed the source no longer matches is exit 2. This is the sensitivity control: the
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
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
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
    objdump -r "$TMPD/$b.o" > "$TMPD/$b.dis.rel" 2>/dev/null || { echo "FAIL(2): objdump -r failed on $b"; exit 2; }
done

python3 - "$TMPD/$EVID.dis" "$TMPD/$VAC.dis" "$EVID" "$VAC" "$REPORT" "$REPO" <<'PY'
import re, sys, os, glob, collections

evid_dis, vac_dis, evid, vac, report, repo = sys.argv[1:7]

# ── THE COUNTING FUNCTIONS, DERIVED FROM SOURCE (ADR 0025 L5) ──────────────
# A function COUNTS when its body writes a count (ArcInner/RcBox `strong` /
# `weak`, a Memoria node's rc header via `node_set_rc`) or calls a function
# that counts; it is ATOMIC when its body calls a read-modify-write extern of
# `logos.lang.atomic` (every `extern fn logos_atomic_*` but load/store) or a
# function that is. Both closures run over stdlib/mem + stdlib/lang. Text names
# no receiver type, so a call by NAME joins the closure only when EVERY
# function of that name is already in it; otherwise only inside its own file.
# Implicit drops in a library body are invisible to text — the object-level
# symbol match (types are in the symbol) is what sees those.
TOK = re.compile(r'\bimpl\b[^{;]*\{|\bfn\s+([A-Za-z_]\w*)[^{;]*[{;]|[{}]')
def functions(path):
    s = re.sub(r'//[^\n]*', '', open(path, errors='replace').read())
    out, impl, impl_depth, depth, i = [], None, -1, 0, 0
    while True:
        m = TOK.search(s, i)
        if not m: return out
        t, i = m.group(0), m.end()
        if t.startswith('impl'):
            h = t[:-1]
            mm = (re.search(r'\bfor\s+([A-Za-z_]\w*)', h) or
                  re.search(r'\bimpl\b(?:\s*<[^>]*(?:<[^>]*>[^>]*)*>)?\s*([A-Za-z_]\w*)', h))
            depth += 1; impl, impl_depth = (mm.group(1) if mm else None), depth
        elif t.startswith('fn'):
            if t.endswith(';'): continue
            d, j = 1, i
            while d and j < len(s):
                d += {'{': 1, '}': -1}.get(s[j], 0); j += 1
            out.append((path, impl if depth == impl_depth else None, m.group(1), s[i:j])); i = j
        elif t == '{': depth += 1
        else:
            if depth == impl_depth: impl, impl_depth = None, -1
            depth -= 1
srcs = sorted(glob.glob(os.path.join(repo, 'stdlib/mem/**/*.logos'), recursive=True) +
              glob.glob(os.path.join(repo, 'stdlib/lang/**/*.logos'), recursive=True))
fns = [f for p in srcs for f in functions(p)]
rmw = sorted(set(re.findall(r'extern fn (logos_atomic_\w+)',
             open(os.path.join(repo, 'stdlib/lang/atomic/atomic.logos')).read())) -
             set(re.findall(r'extern fn (logos_atomic_(?:load|store)\w*)',
             open(os.path.join(repo, 'stdlib/lang/atomic/atomic.logos')).read())))
CALL = re.compile(r'(?:\.|::|\b)([A-Za-z_]\w*)(?:::<[^>]*>)?\(')
defs = collections.defaultdict(set)
for p, ty, nm, _ in fns: defs[nm].add((p, ty, nm))
def closure(seed):
    got = {(p, ty, nm) for p, ty, nm, b in fns if seed.search(b)}
    while True:
        gn = {nm for _, _, nm in got if defs[nm] <= got}
        ln = {(p, nm) for p, _, nm in got}
        new = {(p, ty, nm) for p, ty, nm, b in fns if (p, ty, nm) not in got and
               any(x in gn or (p, x) in ln for x in CALL.findall(b))}
        if not new: return {(ty, nm) for _, ty, nm in got}
        got |= new
COUNT  = closure(re.compile(r'\.(strong|weak)\s*=[^=]|\.(strong|weak)\)\.fetch_\w+\(|\bnode_set_rc\('))
ATOMIC = closure(re.compile(r'\b(' + '|'.join(rmw) + r')\(')) if rmw else set()
# canaries: the derivation reaches every owner, or it measures nothing
for want, st in ((('Arc', 'clone_arc'), COUNT), (('Rc', 'clone_rc'), COUNT), (('Weak', 'upgrade'), COUNT),
                 ((None, 'node_inc_rc'), COUNT), (('MemBlockPtr', 'inc_rc'), COUNT), (('Arc', 'drop_arc'), ATOMIC)):
    if want not in st:
        print(f"FAIL(2): the counting derivation lost its canary {want} — the source moved; re-derive the seeds")
        sys.exit(2)
def symbol_re(st):
    meth, free = collections.defaultdict(set), set()
    for ty, nm in st: (meth[ty].add(nm) if ty else free.add(nm))
    alts = [re.escape(t) + r'(\$G\d+\$\S*?)?__(' + '|'.join(map(re.escape, sorted(m))) + r')__[fg]__'
            for t, m in sorted(meth.items())]
    alts.append(r'(' + '|'.join(map(re.escape, sorted(free))) + r')__[fg]__')
    return re.compile(r'(^|[.$])(' + '|'.join(alts) + r')')
RC, AT = symbol_re(COUNT), symbol_re(ATOMIC)
SETUP= re.compile(r'^main$|__create__|__open__|create_ctr')
WALK = re.compile(r'__ctr_b|__ctr_leafbatch')
FOLD = re.compile(r'\$M[0-9a-f]{16}')

def parse(path):
    # call edges (`call`/`jmp` relocations) per function, and the functions
    # whose address ESCAPES: any other text relocation to them (`lea`, `mov`)
    sec, prev = None, ''
    edges, escaped, funcs = collections.defaultdict(list), set(), []
    for ln in open(path, errors='replace'):
        # Names are classified by their DECLARED spelling: a type's package
        # fold (`$M<hex16>`, ADR 0030 S9b) is stripped first.
        ln = FOLD.sub('', ln.rstrip('\n'))
        m = re.match(r'^Disassembly of section \.text\.(.+):$', ln)
        if m:
            sec = m.group(1); funcs.append(sec); edges[sec]; continue
        if sec is None: continue
        m = re.search(r'\bR_X86_64_\w+\s+(\S+)', ln)
        if m:
            t = re.split(r'[-+]0x', m.group(1))[0]
            op = re.search(r'\t([a-z]+)\s', prev + ' ')
            if op and op.group(1) in ('call', 'jmp'): edges[sec].append(t)
            else: escaped.add(t[6:] if t.startswith('.text.') else t)
        elif re.match(r'^\s+[0-9a-f]+:\t', ln): prev = ln
    return funcs, edges, escaped

def data_escapes(path):
    # a function address stored in data (a vtable, a table) escapes as well
    out, sec = set(), None
    for ln in open(path, errors='replace'):
        m = re.match(r'^RELOCATION RECORDS FOR \[(.+)\]:', ln)
        if m: sec = m.group(1); continue
        if sec and not sec.startswith('.text.') and sec != '.eh_frame':
            m = re.search(r'\bR_X86_64_\w+\s+(\S+)', ln)
            if m:
                t = FOLD.sub('', re.split(r'[-+]0x', m.group(1))[0])
                out.add(t[6:] if t.startswith('.text.') else t)
    return out

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

def reach(start, edges):
    seen, stack = set(), list(start)
    while stack:
        x = stack.pop()
        if x in seen: continue
        seen.add(x)
        stack.extend(edges.get(x, ()))
    return seen

def sites(edges, pat):
    # a site inside a counting function is that function's own body: it is
    # charged to whoever calls the counting function, not counted twice
    return [(s, t) for s, ts in edges.items() for t in ts if pat.search(t) and not pat.search(s)]

fail = 0

# ── THE EVIDENCE ARM ───────────────────────────────────────────────────────
funcs, edges, escaped = parse(evid_dis)
escaped |= data_escapes(evid_dis + '.rel')
names, ents = entries(funcs)
hot = reach(ents, edges)
rev = collections.defaultdict(list)
for s, ts in edges.items():
    for t in ts: rev[t].append(s)
walks = sorted(f for f in hot if WALK.search(f))
print(f"[{evid}] queries={len(names)} entries={len(ents)} functions={len(funcs)} hot-closure={len(hot)}")
print(f"[{evid}] derived from source: {len(COUNT)} counting, {len(ATOMIC)} atomic function(s) ({len(rmw)} RMW externs)")
for label, pat in (("reference-count", RC), ("atomic RMW", AT)):
    ss = sites(edges, pat)
    hs = [x for x in ss if x[0] in hot]
    out = [x for x in ss if not SETUP.search(x[0])]
    # THE DISPATCH-PROOF READING: the closure cannot follow a `dyn` call, so a
    # count outside setup is admitted only when nothing that reaches it has an
    # escaping address — then no indirect call, from anywhere, can enter it.
    open_ = [(s, t) for s, t in out if any(x in escaped and not SETUP.search(x) for x in reach([s], rev))]
    print(f"[{evid}] {label} call sites: object={len(ss)}  hot={len(hs)}  outside-setup={len(out)}"
          f"  reachable-by-dispatch={len(open_)}")
    by = collections.Counter(s for s, _ in ss)
    for k, v in by.most_common(4): print(f"           {v:5d}  {k}")
    # V1 sensitivity: the zero below is a comparison, not an absence of data
    if len(ss) < 1:
        print(f"FAIL(2): [{evid}] the object holds NO {label} call site at all — the hot-path")
        print( "         zero below would be a fact about the fixture.")
        sys.exit(2)
    if hs:
        print(f"FAIL(1): [{evid}] {len(hs)} {label} call site(s) in the per-row path:")
        for s, t in hs[:20]: print(f"           {s}\n             -> {t}")
        fail = 1
    if open_:
        print(f"FAIL(1): [{evid}] {len(open_)} {label} call site(s) outside setup are reachable")
        print( "         from a function whose address escapes (a `dyn` call could enter):")
        for s, t in open_[:20]: print(f"           {s}\n             -> {t}")
        fail = 1
# V2 the query really descends a container
if not walks:
    print(f"FAIL(2): [{evid}] the hot closure contains no container descent symbol —")
    print( "         q1 is not reading a handle, so this is not the arm the audit named.")
    sys.exit(2)
if not names:
    print(f"FAIL(2): [{evid}] no `_run` entry found — no emitted query to measure.")
    sys.exit(2)

# ── THE ARM THE AUDIT REFUTED — CHECKED IN THE VACUITY DIRECTION ───────────
vfuncs, vedges, _ = parse(vac_dis)
vsites = sites(vedges, RC) + sites(vedges, AT)
vsyms  = [f for f in vfuncs if RC.search(f) or AT.search(f)]
print(f"[{vac}] NOT-EVIDENCE arm: counting/atomic symbols={len(vsyms)} call sites={len(vsites)}")
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
