#!/usr/bin/env bash
# rtval_domain_gate.sh <deem-src-dir> <abi-spec>
#
# THE DYNAMIC/INCREMENTAL VALUE DOMAIN, PINNED WHERE IT IS ACTUALLY VISIBLE.
#
# `RtVal` (stdlib/mem/deem/deem.logos) is the value of BOTH the dynamic query
# engine (`Query::run`) and the incremental one, and it is PUBLIC: the UDF/UDA
# registration surface is `fn(&[RtVal]) -> RtVal`. Widening it — an `F32` arm, a
# width beside the payload, an opaque value plus a type handle — is on the table.
# This gate does not decide which; it pins the three MEASURED facts that decide
# whether such a change can land unnoticed.
#
#   1. THE VARIANT SET. Seven arms, derived by parsing the enum rather than
#      listed, so the census below cannot drift from what the type is.
#
#   2. THE ONLY COMPILE-TIME DEFENCE IS THREE EXHAUSTIVE MATCHES. MEASURED
#      2026-08-04 by adding one arm to the real enum and building the stdlib:
#      the compiler named exactly three sites —
#          deem.logos [fn rt_kind]:    match is not exhaustive — missing F32
#          deem.logos [fn rt_truthy]:  match is not exhaustive — missing F32
#          eval.logos [fn RBinds__set_val]: … — missing F32
#      — and the other 24 match sites plus 167 construction sites compiled
#      SILENTLY, because each of those 24 ends in `_`. A new arm falling into a
#      `_` reads as Null / 0 / 0.0 / "" / a null node: a wrong answer, not a
#      compile error. So those three matches ARE the widening's tripwire, and a
#      `_` added to any one of them would retire it without a single test going
#      red. This gate refuses that: the exhaustive set must stay exactly those
#      three, and the wildcard population must stay the size it was measured at.
#
#   3. THE ABI CANNOT SEE ANY OF IT. MEASURED, same experiment: with the extra
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
# ⚠ EVERY CHECK CARRIES A CANARY, IN THE SAME RUN, THROUGH THE SAME CODE PATH.
# Checks 1 and 2 deliver their verdict as text a parser produced, and a parser
# that has stopped parsing produces "no violations" exactly like a clean tree.
# Each canary feeds a deliberately mutated input to the SAME function and
# requires the mutation to come back named.
#
# exit 0 = the facts still hold · 1 = a fact moved (or a canary was not caught)
#        · 2 = IO/usage error.

set -uo pipefail

SRC_DIR="${1:-}"
ABI_SPEC="${2:-}"
if [ -z "$SRC_DIR" ] || [ -z "$ABI_SPEC" ]; then
    echo "usage: rtval_domain_gate.sh <deem-src-dir> <abi-spec>"; exit 2
fi
[ -d "$SRC_DIR" ]  || { echo "rtval-gate: no such dir '$SRC_DIR'";   exit 2; }
[ -f "$ABI_SPEC" ] || { echo "rtval-gate: no such spec '$ABI_SPEC'"; exit 2; }
command -v python3 >/dev/null || { echo "rtval-gate: python3 not found"; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ── MEASURED CONSTANTS ───────────────────────────────────────────────────────
# Read off the tree at a968bc3c on 2026-08-04, by the tool below plus one
# control build with an extra arm. Each is a claim someone can re-run.
WANT_VARIANTS="B Error F I Node Null S"          # sorted; 7 arms
WANT_EXHAUSTIVE="RBinds__set_val rt_kind rt_truthy"   # sorted; the tripwire
WANT_MATCH_SITES=27
WANT_WILDCARD_SITES=24

# ── THE CENSUS TOOL ──────────────────────────────────────────────────────────
# Two commands over Logos sources:
#   variants <file>   — the variant names of `pub enum RtVal`, one per line
#   census <dir|file> — one line per match whose arms are RtVal patterns:
#                       <basename>:<line>\t<EXHAUSTIVE|WILDCARD>\t<enclosing fn>
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

def census_file(path, sink):
    src = strip_comments(open(path).read())
    starts = [0]
    for i, c in enumerate(src):
        if c == '\n': starts.append(i + 1)
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

cmd = sys.argv[1]
tgt = sys.argv[2]
if cmd == "variants":
    print("\n".join(variants(tgt)))
elif cmd == "census":
    files = sorted(glob.glob(os.path.join(tgt, "*.logos"))) if os.path.isdir(tgt) else [tgt]
    sink = []
    for f in files:
        census_file(f, sink)
    print("\n".join(sink))
else:
    sys.stderr.write("bad command\n"); sys.exit(2)
PYEOF

fail=0
say_fail() { echo "::error:: $*"; fail=1; }

# ── 1. THE VARIANT SET ───────────────────────────────────────────────────────
DEEM="$SRC_DIR/deem.logos"
[ -f "$DEEM" ] || { echo "rtval-gate: $DEEM missing"; exit 2; }
got_variants=$(python3 "$WORK/census.py" variants "$DEEM" | sort | tr '\n' ' ' | sed 's/ *$//')
if [ "$got_variants" != "$WANT_VARIANTS" ]; then
    say_fail "RtVal's variant set moved."
    echo "          was:  $WANT_VARIANTS"
    echo "          now:  $got_variants"
    echo "          A change to the value domain of the dynamic AND incremental"
    echo "          engines. Re-run the census below, update the constants in"
    echo "          this gate, and say in the commit which of the 24 wildcard"
    echo "          fallbacks the new arm now silently reaches."
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
fi

# ── 2. THE THREE EXHAUSTIVE MATCHES ──────────────────────────────────────────
python3 "$WORK/census.py" census "$SRC_DIR" > "$WORK/census.txt"
n_sites=$(grep -c . "$WORK/census.txt" || true)
n_wild=$(grep -c $'\tWILDCARD\t' "$WORK/census.txt" || true)
n_part=$(grep -c $'\tPARTIAL\t' "$WORK/census.txt" || true)
got_exh=$(grep $'\tEXHAUSTIVE\t' "$WORK/census.txt" | cut -f3 | sort | tr '\n' ' ' | sed 's/ *$//')

if [ "$got_exh" != "$WANT_EXHAUSTIVE" ]; then
    say_fail "The exhaustive-match set over RtVal moved."
    echo "          was:  $WANT_EXHAUSTIVE"
    echo "          now:  $got_exh"
    echo "          These matches are the ONLY thing that turns a new RtVal arm"
    echo "          into a compile error (MEASURED: a control build with an F32"
    echo "          arm failed at exactly these three and nowhere else). Adding a"
    echo "          \`_\` to any of them makes the next widening silent; removing"
    echo "          one has the same effect. If a fourth appears, that is good"
    echo "          news — record it here."
fi
if [ "$n_sites" != "$WANT_MATCH_SITES" ] || [ "$n_wild" != "$WANT_WILDCARD_SITES" ]; then
    say_fail "The RtVal match census moved: $n_sites sites, $n_wild with a \`_\`"
    echo "          fallback (measured: $WANT_MATCH_SITES sites, $WANT_WILDCARD_SITES wildcard)."
    echo "          A \`_\` arm over RtVal absorbs a future variant as Null / 0 /"
    echo "          0.0 / \"\" — a wrong answer, not a diagnostic. A new one is a"
    echo "          decision; make it in the commit message, then update this"
    echo "          number. Current census:"
    sed 's/^/            /' "$WORK/census.txt"
fi
if [ "$n_part" != "0" ]; then
    say_fail "$n_part match(es) over RtVal are neither exhaustive nor wildcard —"
    echo "          the classifier has no reading for them; inspect:"
    grep $'\tPARTIAL\t' "$WORK/census.txt" | sed 's/^/            /'
fi

# CANARY: a `_` added to an otherwise exhaustive match must be reported WILDCARD,
# and a match that lost an arm must be reported PARTIAL. Both through census().
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
EOF
python3 "$WORK/census.py" census "$WORK/canary_match.logos" > "$WORK/canary_census.txt"
if ! grep -q $'\tWILDCARD\trt_kind_canary' "$WORK/canary_census.txt"; then
    say_fail "CANARY 'wildcard detection' NOT CAUGHT — a \`_\` arm added to a"
    echo "          seven-arm match did not come back WILDCARD. Check 2 above is"
    echo "          then reporting a clean tree it never inspected. Saw:"
    sed 's/^/            /' "$WORK/canary_census.txt"
fi
if ! grep -q $'\tPARTIAL\trt_partial_canary' "$WORK/canary_census.txt"; then
    say_fail "CANARY 'partial detection' NOT CAUGHT — a match missing five arms"
    echo "          did not come back PARTIAL. Saw:"
    sed 's/^/            /' "$WORK/canary_census.txt"
fi

# ── 3. THE ABI BLIND SPOT ────────────────────────────────────────────────────
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
if [ "$n_rec" != "0" ]; then
    say_fail "the ABI spec now carries a \`type logos.mem.deem.RtVal\` record."
    echo "          GOOD NEWS, and a deliberate change: RtVal's shape becomes"
    echo "          visible to scripts/abi-check.sh, so a widening can no longer"
    echo "          answer 'ADDED: 0 record(s) / ABI-PRESERVING' (MEASURED"
    echo "          2026-08-04 with an F32 arm built into the stdlib). Someone"
    echo "          added RtVal to \`is_deem_api_type\` in"
    echo "          src/compiler/emit_module.cpp. Retire this check and say so."
fi

if [ "$fail" != 0 ]; then
    echo "rtval-gate: FAILED"
    exit 1
fi
n_variants=$(python3 "$WORK/census.py" variants "$DEEM" | grep -c .)
echo "rtval-gate: OK — RtVal has $n_variants variants [$got_variants];"
echo "            $n_sites match sites, $n_wild behind a \`_\`, exhaustive = [$got_exh];"
echo "            ABI spec: $n_men record(s) mention RtVal, $n_rec record(s) describe it."
exit 0
