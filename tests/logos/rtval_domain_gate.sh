#!/usr/bin/env bash
# rtval_domain_gate.sh <deem-src-dir> <abi-spec> <fallback-ledger>
#
# THE RUNTIME VALUE DOMAIN, PINNED WHERE IT IS ACTUALLY VISIBLE.
#
# ⚠ RE-PINNED 2026-08-09 BY P5, AND THE HEADER SENTENCE HAD TO CHANGE BECAUSE IT
# STOPPED BEING TRUE. It read: "`RtVal` is the value of BOTH the dynamic query
# engine (`Query::run`) and the incremental one". P5 deleted both engines
# (`stdlib/mem/deem/{check,exec,query,incr,incr_rec,mapping_state}.logos`).
# `RtVal` SURVIVES, in `stdlib/mem/deem/deem.logos`, and it is still PUBLIC —
# the UDF/UDA registration surface is `fn(&[RtVal]) -> RtVal` and `QEnv` still
# carries `f_ptrs: [fn(&[RtVal]) -> RtVal; 8]` — but its consumers are now the
# RUNTIME TEMPLATE ENGINE (`tpl.logos`) and the graph walker (`graphsrc.logos`).
# The two constants below moved with the population and are RE-MEASURED, not
# relaxed: the gate reads SOURCE TEXT, so it went red on the deletion with no
# rebuild, which is the gate working.
#
# `RtVal` is the value of the runtime template engine and of the dynamic graph
# walker, and it is PUBLIC. Widening it — an `F32` arm, a
# width beside the payload, an opaque value plus a type handle — is on the table.
# This gate does not decide which; it pins the MEASURED facts that decide
# whether such a change can land unnoticed.
#
#   1. THE VARIANT SET. Seven arms, derived by parsing the enum rather than
#      listed, so the census below cannot drift from what the type is.
#
#   2. NO `_` ARM OVER `RtVal` EXISTS THAT IS NOT WRITTEN DOWN. This is the
#      check that changed, and the history is the whole argument for it.
#
#      MEASURED 2026-08-04, BEFORE: 27 match sites, 24 ending in `_`, and a
#      control build with an eighth arm named exactly THREE —
#          deem.logos [fn rt_kind]:    match is not exhaustive — missing F32
#          deem.logos [fn rt_truthy]:  match is not exhaustive — missing F32
#          eval.logos [fn RBinds__set_val]: … — missing F32
#      (`eval.logos` was the file's name in 2026-08; the template port `8c5ad0ea`
#      renamed it to `tpl.logos`. This gate globs `<deem-src-dir>/*.logos`, so it
#      followed the rename with no edit — the name above is the historical record.)
#      — while the other 24 compiled SILENTLY, each absorbing the new arm as
#      Null / 0 / 0.0 / "" / a null node. A wrong answer, not a compile error: a
#      three-site tripwire in front of a 27-site surface.
#
#      MEASURED 2026-08-05, AFTER: all 24 `_` arms replaced by or-patterns
#      naming the variants they answer for — same bodies, same variant sets, so
#      no behaviour moved, but the compiler now checks the set. The same control
#      build names 27 sites instead of 3.
#
#      So this gate no longer pins a hand-written list of three exhaustive
#      matches; that list was an artefact of the 24 silent arms and it is what
#      the slice closes. It pins a RULE instead: the exhaustive set is DERIVED
#      as (all match sites) minus (the ledger), and the ledger —
#      tests/logos/rtval_fallback.ledger — is empty. Held in BOTH directions: a
#      new `_` with no ledger line is red, and a ledger line matching no `_` is
#      red. An entry must name the mechanism that makes the default correct, a
#      RUNNING test that exercises that mechanism, and what would delete the
#      entry — the admissibility rule this codebase already uses for the freeze
#      arms and the shared-ref claims.
#
#   3. THE RESIDUE — THE PART A LEDGER OF `_` ARMS CANNOT REACH, PRINTED ON
#      EVERY GREEN RUN. `rt_eq` (deem.logos) and `rt_cmp` (exec.logos) do not
#      `match` on `RtVal` at all: they branch on the i32 CODE that `rt_kind`
#      returns, through `if rt_kind(l) == 4i32 && …` chains. Match-exhaustiveness
#      cannot see an `if`, so after a widening `rt_eq` still answers `false` for
#      two EQUAL values of the new shape — every row its own group in
#      group-by / distinct / join. (`rt_cmp` had the same shape — an unguarded
#      fallthrough to `f64_data_key(rt_f(…))`, sorting every new shape at 0.0 —
#      and it is deleted; the claim is kept here as the record of the class.)
#      A gate that reports only what it can
#      fix is the "phase table whose labels agree while the cost sits outside
#      them all" failure, so the size and shape of this surface is printed
#      whether the run is green or red, and pinned so that it cannot grow
#      quietly.
#
#   4. THE ABI CANNOT SEE ANY OF IT. MEASURED, same experiment: with the extra
#      arm built into the stdlib, `logosc --emit-abi` produced a BYTE-IDENTICAL
#      spec and `logosc --abi-diff` answered "ADDED: 0 record(s) / VERDICT:
#      ABI-PRESERVING — additive only, patchset OK". The cause is not a differ
#      bug: `is_deem_internal_type` (src/compiler/emit_module.cpp) excludes every
#      type in `logos.mem.deem` except the five-name API allowlist
#      (`is_deem_api_type`: Query, SchemaCatalog, QEnv, QRows, QError), and
#      `RtVal` is not on it — so the spec holds NO `type logos.mem.deem.RtVal`
#      record at all, while three of its records SPELL `RtVal` inside their
#      details (two `sym` lines for register_fn/register_agg and the `QEnv` field
#      list). `scripts/abi-check.sh` is therefore blind here by construction, and
#      a reader who assumes otherwise is assuming wrong. The gate pins the hole
#      so that closing it is a deliberate act with a message attached.
#
# ⚠ EVERY CHECK CARRIES A CANARY, IN THE SAME RUN, THROUGH THE SAME CODE PATH —
# and that matters MORE in this form than it did in the last one. The ledger's
# expected content is EMPTY, so "read the ledger and found nothing to complain
# about" and "never opened the ledger" print the identical line. Each canary
# feeds a deliberately mutated input to the SAME function and requires the
# mutation to come back named; the number caught is ACCUMULATED and floored,
# because a sentence claiming N canaries is not a count of them.
#
# exit 0 = the facts still hold · 1 = a fact moved (or a canary was not caught)
#        · 2 = IO/usage error.

set -uo pipefail

SRC_DIR="${1:-}"
ABI_SPEC="${2:-}"
LEDGER="${3:-}"
if [ -z "$SRC_DIR" ] || [ -z "$ABI_SPEC" ] || [ -z "$LEDGER" ]; then
    echo "usage: rtval_domain_gate.sh <deem-src-dir> <abi-spec> <fallback-ledger>"; exit 2
fi
[ -d "$SRC_DIR" ]  || { echo "rtval-gate: no such dir '$SRC_DIR'";   exit 2; }
[ -f "$ABI_SPEC" ] || { echo "rtval-gate: no such spec '$ABI_SPEC'"; exit 2; }
[ -f "$LEDGER" ]   || { echo "rtval-gate: no such ledger '$LEDGER'"; exit 2; }
command -v python3 >/dev/null || { echo "rtval-gate: python3 not found"; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ── MEASURED CONSTANTS ───────────────────────────────────────────────────────
# Read off the tree on 2026-08-05 by the tool below plus two control builds with
# an extra `F32(f32)` arm — one before the expansion (3 sites named), one after
# (27 named). Each is a claim someone can re-run.
WANT_VARIANTS="B Error F I Node Null S"          # sorted; 7 arms
# ⚠ 27/27 → 16/22 at P5 (2026-08-09). NOT a weakening: the sites did not become
# unchecked, the FILES THAT HELD THEM WERE DELETED. Measured after the cut, by
# running this gate and reading its own census print: 16 match sites (9 in
# deem.logos, 2 in graphsrc.logos, 5 in tpl.logos) and 22 `rt_kind` call sites
# (2 in `rt_eq`, 20 in `eval_sexpr`). `rt_cmp` — named in item 3 below as one of
# the two i32-code branchers — lived in `exec.logos` and is GONE; `rt_eq` is the
# one that remains, and its defect is unchanged.
WANT_MATCH_SITES=16                              # matches whose arms are RtVal patterns
WANT_KIND_CALLS=22                               # `rt_kind(…)` CALL sites — the residue
# ⚠ 27, NOT 16, AND THE DIFFERENCE IS A METHOD LESSON. The number carried into
# this slice was 16, taken from `grep -h 'rt_kind(' *.logos | wc -l` — which
# counts LINES. `if rt_kind(l) == 0i32 || rt_kind(r) == 0i32` is one line and
# two dispatches, and eleven of the seventeen lines are that shape. Counted as
# CALLS, over comment-stripped source, the surface is 27 before the expansion
# and 27 after it (verified against `git archive HEAD` — the expansion touched
# no `rt_kind` call). A residue measured 40% small is the same failure the gate
# exists to prevent, one level up.

# ── THE CENSUS TOOL ──────────────────────────────────────────────────────────
# Four commands over Logos sources:
#   variants <file>     — the variant names of `pub enum RtVal`, one per line
#   census <dir|file>   — one line per match whose arms are RtVal patterns:
#                         <basename>:<line>\t<EXHAUSTIVE|WILDCARD|PARTIAL>\t<fn>
#   residue <dir|file>  — one line per `rt_kind(…)` CALL (the definition is not a
#                         call and is excluded): <basename>:<line>\t<fn>
#   reconcile <census-file> <ledger>
#                       — one line per ledger/census disagreement:
#                         UNLEDGERED | STALE | COUNT | MALFORMED
cat > "$WORK/census.py" <<'PYEOF'
import re, sys, os, glob, bisect

ARMS = ["I", "F", "B", "S", "Node", "Null", "Error"]

def strip_comments(src):
    out = []; i = 0; n = len(src); instr = False
    while i < n:
        c = src[i]
        if instr:
            if c == '\\':
                out.append(' '); out.append(' '); i += 2; continue
            if c == '"': instr = False
            out.append(c); i += 1; continue
        if c == '"':
            instr = True; out.append(c); i += 1; continue
        if c == '/' and i + 1 < n and src[i+1] == '/':
            while i < n and src[i] != '\n':
                out.append(' '); i += 1
            continue
        out.append(c); i += 1
    return "".join(out)

def variants(path):
    src = strip_comments(open(path).read())
    m = re.search(r'\bpub\s+enum\s+RtVal\s*\{', src)
    if not m:
        return []
    d = 0; i = m.end() - 1; start = None
    while i < len(src):
        if src[i] == '{':
            d += 1
            if d == 1: start = i + 1
        elif src[i] == '}':
            d -= 1
            if d == 0: break
        i += 1
    body = src[start:i]
    out = []; depth = 0; cur = ""
    for ch in body:
        if ch in '([<': depth += 1
        elif ch in ')]>': depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur); cur = ""
        else:
            cur += ch
    out.append(cur)
    names = []
    for piece in out:
        t = piece.strip()
        if not t: continue
        mm = re.match(r'([A-Za-z_][A-Za-z0-9_]*)', t)
        if mm: names.append(mm.group(1))
    return names

def enclosing_fn(src, pos):
    """Nearest `fn NAME` declaration above `pos`; impl methods reported as
    Type__name when an `impl Type` block encloses them."""
    head = src[:pos]
    fns = list(re.finditer(r'\bfn\s+([A-Za-z_][A-Za-z0-9_]*)', head))
    if not fns:
        return "<toplevel>"
    f = fns[-1]
    name = f.group(1)
    impls = list(re.finditer(r'\bimpl\s+([A-Za-z_][A-Za-z0-9_]*)', head))
    if impls:
        # inside the impl block iff braces have not closed back to its level
        ip = impls[-1].end()
        depth = 0
        for ch in src[ip:f.start()]:
            if ch == '{': depth += 1
            elif ch == '}': depth -= 1
        if depth > 0:
            name = impls[-1].group(1) + "__" + name
    return name

def line_starts(src):
    starts = [0]
    for i, c in enumerate(src):
        if c == '\n': starts.append(i + 1)
    return starts

def census_file(path, sink):
    src = strip_comments(open(path).read())
    starts = line_starts(src)
    for m in re.finditer(r'\bmatch\b', src):
        p = m.end(); depth = 0; ob = None
        while p < len(src):
            c = src[p]
            if c == '(': depth += 1
            elif c == ')': depth -= 1
            elif c == '{' and depth == 0: ob = p; break
            elif c == ';': break
            p += 1
        if ob is None: continue
        d = 0; q = ob
        while q < len(src):
            if src[q] == '{': d += 1
            elif src[q] == '}':
                d -= 1
                if d == 0: break
            q += 1
        body = src[ob+1:q]
        pats = []; d = 0; seg = 0; i = 0
        while i < len(body):
            c = body[i]
            if c in '({[': d += 1
            elif c in ')}]': d -= 1
            elif c == '=' and i + 1 < len(body) and body[i+1] == '>' and d == 0:
                pats.append(body[seg:i].strip())
                j = i + 2
                while j < len(body) and body[j] in ' \t\n': j += 1
                if j < len(body) and body[j] == '{':
                    dd = 0
                    while j < len(body):
                        if body[j] == '{': dd += 1
                        elif body[j] == '}':
                            dd -= 1
                            if dd == 0: j += 1; break
                        j += 1
                else:
                    dd = 0
                    while j < len(body):
                        ch = body[j]
                        if ch in '({[': dd += 1
                        elif ch in ')}]':
                            if dd == 0: break
                            dd -= 1
                        elif ch == ',' and dd == 0: j += 1; break
                        j += 1
                seg = j; i = j; continue
            i += 1
        if not any('RtVal::' in a for a in pats):
            continue
        present = set()
        for a in pats:
            for v in re.findall(r'RtVal::(\w+)', a):
                present.add(v)
        wild = any(re.match(r'^_\w*$', a.strip()) for a in pats)
        missing = [a for a in ARMS if a not in present]
        kind = "WILDCARD" if wild else ("EXHAUSTIVE" if not missing else "PARTIAL")
        line = bisect.bisect_right(starts, m.start())
        sink.append("%s:%d\t%s\t%s" % (os.path.basename(path), line, kind,
                                       enclosing_fn(src, m.start())))

def residue_file(path, sink):
    """`rt_kind(` CALL sites — the i32-code dispatch surface no exhaustiveness
    check reaches. The DEFINITION (`fn rt_kind(`) is not a call and is excluded;
    everything else counts, in an `if` chain, an assignment or an argument."""
    src = strip_comments(open(path).read())
    starts = line_starts(src)
    for m in re.finditer(r'\brt_kind\s*\(', src):
        if re.search(r'\bfn\s+$', src[max(0, m.start() - 8):m.start()]):
            continue
        line = bisect.bisect_right(starts, m.start())
        sink.append("%s:%d\t%s" % (os.path.basename(path), line,
                                   enclosing_fn(src, m.start())))

def read_ledger(path):
    """-> (entries {site: count}, malformed [(lineno, reason)])"""
    entries = {}; bad = []
    for n, raw in enumerate(open(path).read().split("\n"), 1):
        ln = raw.rstrip("\n")
        if not ln.strip() or ln.lstrip().startswith("#"):
            continue
        f = ln.split("\t")
        if len(f) != 5:
            bad.append((n, "%d TAB-separated field(s), want 5 "
                           "(site/count/mechanism/gate/deleted-by)" % len(f)))
            continue
        site, cnt, mech, gate, del_by = [x.strip() for x in f]
        if not re.match(r'^[A-Za-z0-9_.]+:[A-Za-z0-9_]+$', site):
            bad.append((n, "site '%s' is not <file>:<fn>" % site)); continue
        if not cnt.isdigit() or int(cnt) < 1:
            bad.append((n, "count '%s' is not a positive integer" % cnt)); continue
        empty = [nm for nm, v in (("mechanism", mech), ("gate", gate),
                                  ("deleted-by", del_by)) if not v]
        if empty:
            bad.append((n, "%s empty — an entry must name the mechanism, a "
                           "RUNNING gate that exercises it, and what would "
                           "delete it" % "/".join(empty)))
            continue
        entries[site] = entries.get(site, 0) + int(cnt)
    return entries, bad

def reconcile(census_path, ledger_path):
    wild = {}
    for ln in open(census_path).read().split("\n"):
        if not ln.strip(): continue
        f = ln.split("\t")
        if len(f) != 3 or f[1] != "WILDCARD": continue
        key = "%s:%s" % (f[0].split(":")[0], f[2])
        wild[key] = wild.get(key, 0) + 1
    entries, bad = read_ledger(ledger_path)
    out = []
    for n, why in bad:
        out.append("MALFORMED\tline %d\t%s" % (n, why))
    for k in sorted(wild):
        if k not in entries:
            out.append("UNLEDGERED\t%s\t%d `_` arm(s) over RtVal, no ledger entry"
                       % (k, wild[k]))
        elif entries[k] != wild[k]:
            out.append("COUNT\t%s\tcensus %d, ledger %d" % (k, wild[k], entries[k]))
    for k in sorted(entries):
        if k not in wild:
            out.append("STALE\t%s\tledger entry matches no `_` arm" % k)
    return out

def files_of(tgt):
    if os.path.isdir(tgt):
        return sorted(glob.glob(os.path.join(tgt, "*.logos")))
    return [tgt]

cmd = sys.argv[1]
if cmd == "variants":
    print("\n".join(variants(sys.argv[2])))
elif cmd == "census":
    sink = []
    for f in files_of(sys.argv[2]): census_file(f, sink)
    print("\n".join(sink))
elif cmd == "residue":
    sink = []
    for f in files_of(sys.argv[2]): residue_file(f, sink)
    print("\n".join(sink))
elif cmd == "reconcile":
    print("\n".join(reconcile(sys.argv[2], sys.argv[3])))
else:
    sys.stderr.write("bad command\n"); sys.exit(2)
PYEOF

fail=0
canaries=0
say_fail() { echo "::error:: $*"; fail=1; }
caught()   { canaries=$((canaries + 1)); }

# ── 1. THE VARIANT SET ───────────────────────────────────────────────────────
DEEM="$SRC_DIR/deem.logos"
[ -f "$DEEM" ] || { echo "rtval-gate: $DEEM missing"; exit 2; }
got_variants=$(python3 "$WORK/census.py" variants "$DEEM" | sort | tr '\n' ' ' | sed 's/ *$//')
if [ "$got_variants" != "$WANT_VARIANTS" ]; then
    say_fail "RtVal's variant set moved."
    echo "          was:  $WANT_VARIANTS"
    echo "          now:  $got_variants"
    echo "          A change to the value domain of the dynamic AND incremental"
    echo "          engines. Every match over RtVal is exhaustive today, so the"
    echo "          compiler has already named the sites that must answer for the"
    echo "          new shape. TWO THINGS IT DID NOT NAME: rt_eq / rt_cmp, which"
    echo "          dispatch on rt_kind's i32 CODE (see the residue below), and"
    echo "          the two ON-DISK encodings in incr.logos (fskey_of writes the"
    echo "          container-key tag byte, fs_jn_row_enc the journal-row tag)."
    echo "          What tag a new arm gets on disk is a FORMAT decision."
fi

# CANARY: the same parser over an enum with an eighth arm must report eight.
cat > "$WORK/canary_enum.logos" <<'EOF'
package canary;
pub enum RtVal {
    I(i64),
    F(f64),
    F32(f32),
    B(bool),
    S(str),
    Node(WAny),
    Null,
    Error,
}
EOF
canary_v=$(python3 "$WORK/census.py" variants "$WORK/canary_enum.logos" | sort | tr '\n' ' ' | sed 's/ *$//')
if [ "$canary_v" != "B Error F F32 I Node Null S" ]; then
    say_fail "CANARY 'variant set' NOT CAUGHT — the parser did not see an added"
    echo "          arm in a synthetic enum (it answered '$canary_v'). Check 1's"
    echo "          verdict above is therefore about nothing."
else
    caught
fi

# ── 2. NO UNLEDGERED `_`, AND THE EXHAUSTIVE SET IS DERIVED ──────────────────
python3 "$WORK/census.py" census "$SRC_DIR" > "$WORK/census.txt"
n_sites=$(grep -c . "$WORK/census.txt" || true)
n_wild=$(grep -c $'\tWILDCARD\t' "$WORK/census.txt" || true)
n_part=$(grep -c $'\tPARTIAL\t' "$WORK/census.txt" || true)
n_exh=$(grep -c $'\tEXHAUSTIVE\t' "$WORK/census.txt" || true)

python3 "$WORK/census.py" reconcile "$WORK/census.txt" "$LEDGER" > "$WORK/recon.txt"
# ⚠ `-s` IS THE WRONG TEST HERE, AND IT FIRED ON THE FIRST RUN: `print("")` for
# an empty verdict leaves a one-byte file, so "no disagreements" read as "a
# disagreement I cannot name". Count NON-BLANK lines.
n_recon=$(grep -c . "$WORK/recon.txt" || true)
if [ "$n_recon" != "0" ]; then
    say_fail "THE LEDGER AND THE TREE DISAGREE about the surviving \`_\` arms over"
    echo "          RtVal (ledger: $LEDGER):"
    sed 's/^/            /' "$WORK/recon.txt"
    echo "          UNLEDGERED = a new silent fallback. A \`_\` over RtVal absorbs"
    echo "          a future variant as Null / 0 / 0.0 / \"\" — a wrong answer, not"
    echo "          a diagnostic — and it retires the compile-time tripwire for"
    echo "          that site without a single test going red. Either NAME the"
    echo "          variants the arm answers for (an or-pattern: same body, same"
    echo "          set, and a widening then fails to COMPILE here), or add a"
    echo "          ledger line naming the mechanism, a RUNNING gate that"
    echo "          exercises it, and what would delete the entry."
    echo "          STALE = an excuse outliving its arm; delete the line."
    echo "          COUNT = that (file, fn) holds a different number of \`_\` arms"
    echo "          than the ledger admits."
    echo "          MALFORMED = the line cannot be read as an entry at all."
fi

# The exhaustive set is DERIVED, not listed: every censused site the ledger does
# not cover must be exhaustive. The old form named three functions by hand — a
# list that was the artefact of 24 silent arms, and the thing this closes.
want_exh=$((n_sites - n_wild - n_part))
if [ "$n_exh" != "$want_exh" ] || [ "$n_part" != "0" ]; then
    say_fail "the census does not classify: $n_sites sites = $n_exh exhaustive +"
    echo "          $n_wild wildcard + $n_part partial. A PARTIAL match is one the"
    echo "          classifier has no reading for — neither exhaustive nor a"
    echo "          fallback — and it must be inspected, not counted:"
    grep $'\tPARTIAL\t' "$WORK/census.txt" | sed 's/^/            /'
fi
if [ "$n_sites" != "$WANT_MATCH_SITES" ]; then
    say_fail "the RtVal match census moved: $n_sites sites (measured: $WANT_MATCH_SITES)."
    echo "          Not necessarily wrong — a site can be added or removed for"
    echo "          good reason — but it is the population every claim above is"
    echo "          about, so it is said in the commit and re-measured here."
    echo "          Current census:"
    sed 's/^/            /' "$WORK/census.txt"
fi

# ── CANARIES for check 2, all through the SAME two functions ─────────────────
# (a) classification: a `_` added to an otherwise exhaustive match must come
#     back WILDCARD; a match that lost arms must come back PARTIAL; and an
#     or-pattern covering the rest must come back EXHAUSTIVE — that last is the
#     exact shape all 24 expanded fallbacks use, and a classifier that could not
#     read it would call them PARTIAL and make the derived count nonsense.
cat > "$WORK/canary_match.logos" <<'EOF'
package canary;
fn rt_kind_canary(v: RtVal) -> i32 {
    match v {
        RtVal::I(_x)    => { return 1i32; }
        RtVal::F(_x)    => { return 2i32; }
        RtVal::B(_x)    => { return 3i32; }
        RtVal::S(_x)    => { return 4i32; }
        RtVal::Node(_x) => { return 5i32; }
        RtVal::Null     => { return 0i32; }
        RtVal::Error    => { return 6i32; }
        _               => { return 9i32; }
    }
}
fn rt_partial_canary(v: RtVal) -> i32 {
    match v {
        RtVal::I(_x)    => { return 1i32; }
        RtVal::F(_x)    => { return 2i32; }
    }
}
fn rt_orpat_canary(v: RtVal) -> i32 {
    match v {
        RtVal::I(_x) => { return 1i32; }
        RtVal::F(_) | RtVal::B(_) | RtVal::S(_) | RtVal::Node(_)
          | RtVal::Null | RtVal::Error => { return 0i32; }
    }
}
EOF
python3 "$WORK/census.py" census "$WORK/canary_match.logos" > "$WORK/canary_census.txt"
if ! grep -q $'\tWILDCARD\trt_kind_canary' "$WORK/canary_census.txt"; then
    say_fail "CANARY 'wildcard detection' NOT CAUGHT — a \`_\` arm added to a"
    echo "          seven-arm match did not come back WILDCARD. Check 2 above is"
    echo "          then reporting a clean tree it never inspected. Saw:"
    sed 's/^/            /' "$WORK/canary_census.txt"
else
    caught
fi
if ! grep -q $'\tPARTIAL\trt_partial_canary' "$WORK/canary_census.txt"; then
    say_fail "CANARY 'partial detection' NOT CAUGHT — a match missing five arms"
    echo "          did not come back PARTIAL. Saw:"
    sed 's/^/            /' "$WORK/canary_census.txt"
else
    caught
fi
if ! grep -q $'\tEXHAUSTIVE\trt_orpat_canary' "$WORK/canary_census.txt"; then
    say_fail "CANARY 'or-pattern is exhaustive' NOT CAUGHT — a match covering all"
    echo "          seven arms through ONE or-pattern did not come back"
    echo "          EXHAUSTIVE. That is the shape of every expanded fallback in"
    echo "          the tree, so the derived count above would be counting them"
    echo "          wrong. Saw:"
    sed 's/^/            /' "$WORK/canary_census.txt"
else
    caught
fi

# (b) THE LEDGER IS BEING READ AT ALL. Its expected content is EMPTY, so "read
#     it, nothing to report" and "never opened it" produce identical output.
#     These three feed synthetic census/ledger pairs to the SAME `reconcile`.
printf 'x.logos:12\tWILDCARD\tsome_fn\n' > "$WORK/c_wild.txt"
: > "$WORK/l_empty.ledger"
python3 "$WORK/census.py" reconcile "$WORK/c_wild.txt" "$WORK/l_empty.ledger" > "$WORK/k1.txt"
if ! grep -q $'^UNLEDGERED\tx.logos:some_fn' "$WORK/k1.txt"; then
    say_fail "CANARY 'unledgered \`_\`' NOT CAUGHT — a wildcard site absent from"
    echo "          the ledger did not come back named, so check 2's silence is"
    echo "          about a comparison that never ran. Saw:"
    sed 's/^/            /' "$WORK/k1.txt"
else
    caught
fi
printf 'a.logos:nope\t1\tmech\tsome_gate\tgrounds\n' > "$WORK/l_stale.ledger"
: > "$WORK/c_empty.txt"
python3 "$WORK/census.py" reconcile "$WORK/c_empty.txt" "$WORK/l_stale.ledger" > "$WORK/k2.txt"
if ! grep -q $'^STALE\ta.logos:nope' "$WORK/k2.txt"; then
    say_fail "CANARY 'stale ledger line' NOT CAUGHT — an entry matching no \`_\`"
    echo "          arm did not come back named, so the ledger can only grow. Saw:"
    sed 's/^/            /' "$WORK/k2.txt"
else
    caught
fi
printf 'a.logos:f\t1\tmech\t\tgrounds\n' > "$WORK/l_bad.ledger"
python3 "$WORK/census.py" reconcile "$WORK/c_empty.txt" "$WORK/l_bad.ledger" > "$WORK/k3.txt"
if ! grep -q '^MALFORMED' "$WORK/k3.txt"; then
    say_fail "CANARY 'malformed ledger entry' NOT CAUGHT — an entry with an empty"
    echo "          \`gate\` field was accepted, so the admissibility rule (name"
    echo "          the mechanism, name a RUNNING gate that exercises it, say"
    echo "          what would delete the entry) is decoration. Saw:"
    sed 's/^/            /' "$WORK/k3.txt"
else
    caught
fi

# ── 3. THE RESIDUE — THE SURFACE THIS LEDGER CANNOT REACH ────────────────────
python3 "$WORK/census.py" residue "$SRC_DIR" > "$WORK/residue.txt"
n_kind=$(grep -c . "$WORK/residue.txt" || true)
residue_fns=$(cut -f2 "$WORK/residue.txt" | sort | uniq -c | sort -rn \
              | awk '{printf "%s(%s) ", $2, $1}')
if [ "$n_kind" != "$WANT_KIND_CALLS" ]; then
    say_fail "the \`rt_kind\` i32-CODE dispatch surface moved: $n_kind call site(s)"
    echo "          (measured: $WANT_KIND_CALLS). This is the surface the ledger CANNOT"
    echo "          make loud — \`if rt_kind(x) == Ni32\` chains, not matches, so"
    echo "          exhaustiveness never reaches them. SHRINKING it is progress"
    echo "          (say which chain became a match); GROWING it widens the part"
    echo "          of the value domain a widening passes through silently."
    echo "          Current sites:"
    sed 's/^/            /' "$WORK/residue.txt"
fi

# CANARY: the residue counter must count. A synthetic file with three calls and
# one DEFINITION must answer three — four means the definition is counted as a
# call, zero means the regex stopped matching.
cat > "$WORK/canary_residue.logos" <<'EOF'
package canary;
fn rt_kind(v: RtVal) -> i32 { return 0i32; }
fn cmp_canary(l: RtVal, r: RtVal) -> i64 {
    if rt_kind(l) == 4i32 && rt_kind(r) == 4i32 { return 0i64; }
    return rt_kind(l) as i64;
}
EOF
canary_res=$(python3 "$WORK/census.py" residue "$WORK/canary_residue.logos" | grep -c . || true)
if [ "$canary_res" != "3" ]; then
    say_fail "CANARY 'residue counter' NOT CAUGHT — a synthetic file with three"
    echo "          rt_kind CALLS and one definition counted $canary_res, not 3."
    echo "          Check 3's number is then not about the dispatch surface."
else
    caught
fi

# ── 4. THE ABI BLIND SPOT ────────────────────────────────────────────────────
abi_type_records() { grep -c "^type	logos.mem.deem.RtVal	" "$1" 2>/dev/null || true; }
abi_mentions()     { grep -c "RtVal" "$1" 2>/dev/null || true; }

n_rec=$(abi_type_records "$ABI_SPEC")
n_men=$(abi_mentions "$ABI_SPEC")
if [ "$n_men" -lt 1 ]; then
    say_fail "the ABI spec no longer mentions RtVal at all ($ABI_SPEC)."
    echo "          The UDF/UDA registration surface (\`fn(&[RtVal]) -> RtVal\`)"
    echo "          is public; if it stopped being spelled in the spec, either"
    echo "          the surface moved or --emit-abi stopped covering it."
fi
# ⚠ THIS CHECK WAS INVERTED ON 2026-08-05, WHICH IS WHAT IT ASKED FOR. It used
# to assert `n_rec == 0` — that the spec carried NO record for RtVal — and its
# failure text said "GOOD NEWS ... retire this check and say so". The hole is
# closed, so the check now asserts the OPPOSITE and stays load-bearing instead
# of being deleted.
#
# It was NOT closed the way that text guessed. Nobody added RtVal to
# `is_deem_api_type` — that list still holds exactly its five original names and
# `grep -rn '"RtVal"' src/` is empty. It was closed by a DERIVED CLOSURE: every
# type named in a recorded field list or enum payload must itself have a record.
# So the population is not a list anyone maintains, and a NEW public type reached
# from the API gets a record without anyone remembering to ask.
#
# MEASURED both before and after, with an F32 arm built into the real enum:
# before, `--emit-abi` produced a byte-identical spec and abi-check answered
# "ADDED: 0 record(s) / ABI-PRESERVING"; after, rc 1 and ABI-BREAKING naming the
# variant list. That blindness is the reason the record has to stay.
if [ "$n_rec" -lt 1 ]; then
    say_fail "the ABI spec has NO \`type logos.mem.deem.RtVal\` record."
    echo "          RtVal is the public UDF/UDA value type, and without a record"
    echo "          abi-check cannot see its shape change: an added or retyped"
    echo "          arm answers 'ADDED: 0 record(s) / ABI-PRESERVING' (MEASURED"
    echo "          2026-08-04, byte-identical spec). If the closure that derives"
    echo "          this record was narrowed or removed, the gate protecting"
    echo "          decision D3's representation change is gone with it."
fi

# ── THE CANARY COUNT IS ACCUMULATED, NOT ASSERTED ────────────────────────────
# The layout gate's closing line once said "NINE canaries caught" and listed
# nine names while twelve had fired; a sentence is not a count. A canary that
# stops firing has already set `fail` above — this floor catches the OTHER
# direction, a check deleted together with the canary that proved it bites.
MIN_CANARIES=8
if [ "$canaries" -lt "$MIN_CANARIES" ]; then
    say_fail "only $canaries canary/canaries fired, floor is $MIN_CANARIES — a check was"
    echo "          removed together with the canary that proved it bites."
fi

if [ "$fail" != 0 ]; then
    echo "rtval-gate: FAILED"
    exit 1
fi
n_variants=$(python3 "$WORK/census.py" variants "$DEEM" | grep -c .)
echo "rtval-gate: OK — RtVal has $n_variants variants [$got_variants];"
echo "            $n_sites match sites: $n_exh exhaustive, $n_wild behind a \`_\`,"
echo "            $n_part partial (ledger: $LEDGER);"
echo "            RESIDUE — $n_kind \`rt_kind\` i32-code call site(s) that NO"
echo "            match-exhaustiveness reaches: $residue_fns"
echo "            ABI spec: $n_men record(s) mention RtVal, $n_rec record(s) describe it;"
echo "            $canaries canaries caught."
exit 0
