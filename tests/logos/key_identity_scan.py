#!/usr/bin/env python3
"""key_identity_scan.py SRC INC [--raw | --guards LEDGER]

The census engine behind key_identity_lint.sh. Read that file's header for the
argument; this one carries only the mechanics.

Default mode prints the census, one row per (file, registry, cell, key), TAB
separated, with a count. Line numbers are DELIBERATELY not in the row: they
drift with every edit above them, and a ledger keyed on them would go red for
reasons that have nothing to do with the class. The normalised key expression
is the site's identity and it also documents the row.
"""
import re, sys, glob, os
from collections import defaultdict

# Container spellings. `StrMap`/`StrSet` are the project's own aliases over
# unordered_map/set<std::string,…> (include/logos/compiler/str_map.hpp) and are
# invisible to a `<std::string` grep — `impls_`, `traits_`, `coherence_keys_`
# and `cfg_features_` are all spelled that way.
CONT = re.compile(r'(?:StrMap<|StrSet\b|(?:std::)?(?:unordered_)?(?:multi)?(?:map|set)<\s*(?:std::)?string)')
# A member/registry identifier: this codebase's convention is a trailing `_`.
# A function LOCAL that happens to end in `_` is over-counted, never missed —
# the safe direction, same choice separator_split_lint.sh makes for `/* */`.
DECLNAME = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*_)\s*[;={]\s*$')
# ── THE QUALIFIER SETS ARE DERIVED, NOT LISTED ──────────────────────────────
# The two ROOT package-qualifying primitives. sema_impl.hpp::sema_key aborts on
# a `::` in its pkg operand; mlir_gen_impl.hpp::qualify_pkg is its mlir-gen twin.
# Everything else is grown from these by fixpoint over the tree, because a
# hand-written list of "safe wrappers" is exactly the drift this whole class is
# made of: `mlir_struct_key(t)` IS `qualify_pkg(t.pkg_name(), …)` under another
# name, and a lint that did not know that would file eleven qualified sites as
# bare and become a ledger nobody believes.
QUAL_ROOTS = ('sema_key', 'qualify_pkg')
# A function DEFINITION header. ANCHORED at the start of a line and indented at
# most 4 — a member of a class body or a free function, never a statement. The
# unanchored form matched `if (…) {` and sliced statement bodies out as
# functions, which put `if` and `lower_stmt_inner` in the keymaker set: MEASURED,
# and the reason for the anchor and the keyword refusal below.
FNDEF = re.compile(r'^[ ]{0,4}(?![ ]*(?:if|for|while|switch|catch|do|else|return)\b)'
                   r'(?:[A-Za-z_][A-Za-z0-9_:<>,&*\[\] \t]*?[ \t*&>])?'
                   r'([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{',
                   re.M)
# A function is not a lookup PRIMITIVE past this size. Bounding the probe set by
# body length is deliberate under-inclusion: without it, any bare lookup that
# happens to follow a call into a 600-line routine containing one `sema_key`
# somewhere would read as the safe bare-LAST half of a qualified-first probe.
PROBE_MAX_LINES = 40
# An ENTITY-NAME PRODUCER, derived from the call text: any callee ending in
# `name`, plus `type_str`. Covers struct_name/enum_name/trait_name/pkg_name/
# type_var_name/concrete_struct_name/link_name without listing them.
PROD = re.compile(r'\b(?:[A-Za-z_][A-Za-z0-9_]*name|type_str)\s*\(')
# A key that IS a mangled link name carries module and package by construction.
MANGLED = re.compile(r'\bsym::|\blink_name\s*\(')
ACCESS_METHODS = ('find','count','at','erase','contains','insert_or_assign','try_emplace','emplace','insert')
GROUND = re.compile(r'KEY-IDENTITY:\s*(?:(OPEN)\s*#(\d+))?')
# How far back a qualified probe may sit and still make this the BARE FALLBACK
# of a qualified-first lookup rather than a bare lookup of its own.
ORDER_WINDOW = 900
# How far above the site the ground comment may sit.
GROUND_LINES = 12


def strip_line_comments(t):
    return "\n".join((ln[:ln.find("//")] if ln.find("//") >= 0 else ln)
                     for ln in t.split("\n"))


def balanced(t, i):
    """Text inside the bracket that opens at t[i]."""
    op = t[i]; cl = ')' if op == '(' else ']'
    d = 0; j = i
    while j < len(t):
        if t[j] == op: d += 1
        elif t[j] == cl:
            d -= 1
            if d == 0: return t[i+1:j]
        j += 1
    return t[i+1:]


def first_arg(s):
    d = 0
    for k, ch in enumerate(s):
        if ch in '([{': d += 1
        elif ch in ')]}': d -= 1
        elif ch == ',' and d == 0: return s[:k]
    return s


def load(src, inc):
    files = sorted(glob.glob(src+"/*.cpp")) + sorted(glob.glob(src+"/*.hpp")) \
          + sorted(glob.glob(src+"/*.inc"))
    incs  = sorted(glob.glob(inc+"/*.hpp"))
    raw, code = {}, {}
    for f in files + incs:
        r = open(f, encoding='utf-8', errors='replace').read()
        raw[f] = r; code[f] = strip_line_comments(r)
    return files, incs, raw, code


def registries(code, files, incs):
    regs = set()
    for f in files + incs:
        t = code[f]
        for m in CONT.finditer(t):
            seg = t[m.start():m.start()+400]
            end = len(seg)
            for j, ch in enumerate(seg):
                if ch in ';{': end = j+1; break
            mm = DECLNAME.search(seg[:end])
            if mm: regs.add(mm.group(1))
    return regs


def _bodies(code):
    """(name, body) for every function definition in the tree, by brace match."""
    out = []
    for f, t in code.items():
        for m in FNDEF.finditer(t):
            ob = m.end() - 1
            d = 0; j = ob
            while j < len(t):
                if t[j] == '{': d += 1
                elif t[j] == '}':
                    d -= 1
                    if d == 0: break
                j += 1
            out.append((m.group(1), t[ob:j]))
    return out


def qualifier_sets(code):
    """Two DERIVED sets, both least fixpoints seeded by QUAL_ROOTS.

    KEYMAKERS — functions that RETURN a package-qualified key. A key expression
      built by one of these carries the package, so the site is not a subject at
      all. The rule is `some return statement contains a keymaker call`:
      `mlir_struct_key` returns `{}` for a null TypeRef and `qualify_pkg(...)`
      otherwise, and the empty string is unmatchable, so an empty return does
      not weaken the claim.

    PROBES — functions that CONSULT a qualified key anywhere in their body, size
      bounded by PROBE_MAX_LINES. `find_struct_it` is the archetype: it probes
      `mlir_struct_key(t)` first and falls back to the bare name, and its own
      comment records that the bare slot is a first-registered-wins alias. A
      call to one of these ahead of a bare lookup is what makes that lookup the
      documented BARE-LAST half of a qualified-FIRST probe rather than a bare
      lookup of its own.
    """
    bodies = _bodies(code)
    keymakers = set(QUAL_ROOTS)
    for _ in range(8):                      # converges in 2-3; bounded, never spins
        kre = re.compile(r'\b(?:' + '|'.join(sorted(keymakers)) + r')\s*\(')
        grew = False
        for name, body in bodies:
            if name in keymakers: continue
            if any(kre.search(r) for r in re.findall(r'return\b[^;]*;', body)):
                keymakers.add(name); grew = True
        if not grew: break
    # PROBES are ONE HOP off the converged keymakers, never a fixpoint of their
    # own. A transitive closure here is a runaway: every small function that
    # calls a probe becomes a probe, and within three rounds the whole compiler
    # is "qualified" and the census reads 41 rows of cell O — MEASURED, which is
    # why this is one hop and says so.
    kre = re.compile(r'\b(?:' + '|'.join(sorted(keymakers)) + r')\s*\(')
    probes = set(QUAL_ROOTS) | {name for name, body in bodies
                                if body.count("\n") <= PROBE_MAX_LINES and kre.search(body)}
    mk = lambda s: re.compile(r'\b(?:' + '|'.join(sorted(re.escape(x) for x in s)) + r')\s*\(')
    return mk(keymakers), mk(probes), keymakers, probes


def census(src, inc):
    files, incs, raw, code = load(src, inc)
    regs = registries(code, files, incs)
    if not regs:
        return None, regs
    KEYMAKER, PROBE, _km, _pr = qualifier_sets(code)
    acc = re.compile(r'\b(' + '|'.join(sorted(re.escape(r) for r in regs)) +
                     r')\s*(?:\.\s*(?:' + '|'.join(ACCESS_METHODS) + r')\s*\(|\[)')
    rows = defaultdict(int)
    for f in files:                     # .cpp/.hpp/.inc under src/compiler
        t = code[f]; rawt = raw[f]; b = os.path.basename(f)
        rawlines = rawt.split("\n")
        for m in acc.finditer(t):
            key = " ".join(first_arg(balanced(t, m.end()-1)).split())
            if KEYMAKER.search(key) or not PROD.search(key):
                continue
            if MANGLED.search(key):
                cell = 'Q'
            elif PROBE.search(t[max(0, m.start()-ORDER_WINDOW):m.start()]):
                cell = 'O'
            else:
                # Ground comment within GROUND_LINES above, read from the
                # UNSTRIPPED text — the ground is a comment by construction.
                ln = _line_of(t, m.start())
                blk = "\n".join(rawlines[max(0, ln-1-GROUND_LINES):ln])
                g = GROUND.search(blk)
                if g and g.group(1): cell = 'B#' + g.group(2)
                elif g:              cell = 'D'
                else:                cell = '!UNGROUNDED'
            rows[(b, m.group(1), cell, key)] += 1
    return rows, regs


def _line_of(code_t, off):
    """1-based line of a code-text offset. Stripping // comments never removes
    a newline, so offsets and line numbers agree between the stripped and the
    raw text — which is what lets the ground comment be read from the raw one."""
    return code_t[:off].count("\n") + 1


def guards(src, ledger):
    """FACT 3 — the scan-by-name guards, asserted structurally."""
    ok = True
    pins = {}
    for ln in open(ledger, encoding='utf-8'):
        m = re.match(r'#GUARD\s+(\S+)\s+(\S+)\s*$', ln.strip())
        if m: pins[m.group(1)] = m.group(2)
    se = os.path.join(src, 'sema_expr.cpp')
    mg = os.path.join(src, 'mlir_gen_expr.cpp')
    for p in (se, mg):
        if not os.path.exists(p):
            print("FAIL(2): guard subject does not resolve: %s" % p); return False
    t = strip_line_comments(open(se, encoding='utf-8', errors='replace').read())

    def span(anchor):
        a = t.find(anchor)
        if a < 0: return None
        ob = t.find('{', a); d = 0; j = ob
        while j < len(t):
            if t[j] == '{': d += 1
            elif t[j] == '}':
                d -= 1
                if d == 0: return (ob, j)
            j += 1
        return None

    guard = span('if (!builtin_shadowed) {')
    if guard is None:
        print("FAIL: the `if (!builtin_shadowed)` guard is GONE from sema_expr.cpp.")
        print("      Without it the bare-callee comparisons in lower_call's early block")
        print("      run before resolve_function_call again, and a user `fn popcount_u64`")
        print("      compiles to llvm.ctpop while its own body is emitted and never called.")
        return False
    lti = span('SemaChecker::lower_type_intrinsic')
    if lti is None:
        print("FAIL: SemaChecker::lower_type_intrinsic not found — guard unpinnable.")
        return False
    at = [m.start() for m in re.finditer(r'\bcallee\s*==\s*"', t)]
    n_guard = sum(1 for p in at if guard[0] <= p < guard[1])
    n_lti   = sum(1 for p in at if lti[0]   <= p < lti[1])
    n_other = len(at) - n_guard - n_lti
    # The two guarded regions are pinned so a comparison MOVING between them is
    # seen; `other` is the one that matters most — a bare-name intrinsic
    # comparison written outside BOTH guards is the wrong-answer instance
    # regrowing, and it moves this number even though the file total also moves.
    for nm, val in (('sema_expr_callee_eq_in_lower_call_guard', n_guard),
                    ('sema_expr_callee_eq_in_lower_type_intrinsic', n_lti),
                    ('sema_expr_callee_eq_unguarded', n_other)):
        want = pins.get(nm)
        if want is None:
            print("FAIL(2): ledger carries no #GUARD pin for %s" % nm); ok = False
        elif str(val) != want:
            print("FAIL: %s is %d, ledger pins %s." % (nm, val, want))
            print("      A bare-callee comparison that moved OUT of a guard, or a new one")
            print("      written outside both, is the wrong-answer instance regrowing:")
            print("      a user `fn popcount_u64` became llvm.ctpop while its own body was")
            print("      emitted and never called. Put it inside a guard, or move the pin")
            print("      deliberately and say why.")
            ok = False
    if 'if (builtin_name_shadowed(callee)) return std::nullopt;' not in t:
        print("FAIL: lower_type_intrinsic's `builtin_name_shadowed` early return is gone.")
        ok = False
    g = strip_line_comments(open(mg, encoding='utf-8', errors='replace').read())
    a2 = g.find('auto bare_intrinsic = [&]() -> std::string {')
    if a2 < 0:
        print("FAIL: mlir_gen_expr.cpp `bare_intrinsic` lambda not found — guard unpinnable.")
        return False
    if 'if (!intrinsic_slot_owned_by_stdlib) return std::string{};' not in g[a2:a2+400]:
        print("FAIL: `bare_intrinsic` no longer yields to a non-stdlib package —")
        print("      the package-stripping fallback is unguarded again.")
        ok = False
    return ok


# ── FACT 4: THE SCAN-BY-NAME POPULATION, TREE-WIDE ──────────────────────────
# The lint's header used to say "no regex can census a name-scan", and FACT 3
# therefore pinned the guards in `sema_expr.cpp` ALONE. That claim was REFUTED
# by this round's own verify: a bare-name intercept written in ANY OTHER FILE
# was invisible to the gate, and `mlir_gen.cpp`'s `if (name == "AnyVal")` — a
# SILENT WRONG ANSWER, the same shape as the `popcount_u64` instance FACT 3 was
# built for — sat there while the gate read green. A gate blind to the shape
# that produced the defect is worse than no gate, because its green vouches.
#
# It CAN be censused: not by classifying each site (that is what needs a human),
# but by pinning the POPULATION PER FILE plus a ROSTER DIGEST. A new intercept
# moves its file's count; a literal swapped for another literal in the same file
# moves the digest with the count unchanged. Both are red at birth, in whatever
# file they are born. Classification then happens at the pin move, deliberately.
#
# The LHS set is name-ish BY SHAPE, not a list of blessed literals: an entity
# name arrives either in a variable whose name says so, or straight out of a
# `…name()` / `target_type()` / `type_str()` accessor.
SCAN_LHS = re.compile(
    r'\b(?:callee|name|cn|base|bare|sname|tname|fname|tn|nm|target|cname|'
    r'struct_name|trait_name|type_name|fn_name|callee_name)\s*==\s*"([^"\\]*)"')
SCAN_ACC = re.compile(
    r'\.(?:name|target_type|trait_name|struct_name|callee|type_name|type_str)'
    r'\(\)\s*==\s*"([^"\\]*)"')


def scans(src, inc):
    """FACT 4 — one row per FILE: how many bare-name intercepts it holds and a
    digest of WHICH names, so a swap at a constant count is still red."""
    import hashlib
    out = []
    files = sorted(glob.glob(os.path.join(src, '*.cpp')) +
                   glob.glob(os.path.join(src, '*.hpp')) +
                   glob.glob(os.path.join(inc, '*.hpp')))
    if not files:
        return None
    for p in files:
        t = strip_line_comments(open(p, encoding='utf-8', errors='replace').read())
        lits = SCAN_LHS.findall(t) + SCAN_ACC.findall(t)
        if not lits:
            continue
        h = hashlib.md5('\n'.join(sorted(lits)).encode()).hexdigest()[:8]
        out.append((os.path.basename(p), len(lits), h))
    return out


def check_scans(src, inc, ledger):
    got = scans(src, inc)
    if got is None:
        print("FAIL(2): FACT 4 subject does not resolve — no sources under %s" % src)
        return 2
    if not got:
        print("FAIL(2): FACT 4 censused ZERO bare-name intercepts over %d files."
              % len(glob.glob(os.path.join(src, '*.cpp'))))
        print("      The population is never empty in this compiler; reading it as")
        print("      zero means the scanner stopped matching, not that the class")
        print("      was closed. Refusing to vouch.")
        return 2
    pins = {}
    for ln in open(ledger, encoding='utf-8'):
        m = re.match(r'#SCAN\s+(\S+)\s+(\d+)\s+(\S+)\s*$', ln.strip())
        if m:
            pins[m.group(1)] = (int(m.group(2)), m.group(3))
    if not pins:
        print("FAIL(2): the ledger carries no #SCAN rows — FACT 4 is unpinned.")
        return 2
    rc = 0
    seen = set()
    for fn, n, h in got:
        seen.add(fn)
        want = pins.get(fn)
        if want is None:
            print("FAIL: %s holds %d bare-name intercept(s) and the ledger pins NONE." % (fn, n))
            print("      A comparison of an entity NAME against a literal is the")
            print("      wrong-answer shape: the intercept fires for whoever spelled")
            print("      that name, so a user declaration of it silently inherits the")
            print("      compiler's meaning (`fn popcount_u64` became llvm.ctpop;")
            print("      `struct AnyVal` was miscompiled to a garbage field read).")
            print("      Guard it on the owning package, or add a #SCAN row saying why.")
            rc = 1
        elif (n, h) != want:
            what = ("count %d, ledger pins %d" % (n, want[0])) if n != want[0] else \
                   ("same count %d but a DIFFERENT roster (%s vs pinned %s) — a name was"
                    " swapped for another name" % (n, h, want[1]))
            print("FAIL: %s bare-name intercepts: %s." % (fn, what))
            print("      Classify the new site (guarded on the owning package? a")
            print("      carried decision? a defect?), then move the pin and say why.")
            rc = 1
    for fn, want in sorted(pins.items()):
        if fn not in seen:
            print("FAIL: the ledger pins %s at %d bare-name intercepts and the tree"
                  " holds none." % (fn, want[0]))
            print("      A pin whose subject vanished must be RETIRED deliberately —")
            print("      left standing it is a row that can never go red again.")
            rc = 1
    return rc


def main():
    src, inc = sys.argv[1], sys.argv[2]
    rest = sys.argv[3:]
    if rest and rest[0] == '--guards':
        sys.exit(0 if guards(src, rest[1]) else 1)
    if rest and rest[0] == '--scans':
        sys.exit(check_scans(src, inc, rest[1]))
    if rest and rest[0] == '--scan-raw':
        rows = scans(src, inc)
        if not rows:
            sys.exit(2)
        for fn, n, h in rows:
            print("#SCAN %s %d %s" % (fn, n, h))
        sys.exit(0)
    rows, regs = census(src, inc)
    if rows is None:
        print("FAIL(2): no string-keyed registries found under %s" % src, file=sys.stderr)
        sys.exit(2)
    for (b, reg, cell, key), n in sorted(rows.items()):
        print("%s\t%s\t%s\t%d\t%s" % (b, reg, cell, n, key))


if __name__ == '__main__':
    main()
