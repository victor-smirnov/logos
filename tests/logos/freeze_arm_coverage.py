#!/usr/bin/env python3
"""freeze_arm_coverage.py — THE FREEZE LATTICE'S POPULATION IS DERIVED, NOT LISTED.

    usage: freeze_arm_coverage.py <repo-root> [--ledger P] [--check P]

`type_is_freeze` (src/compiler/mlir_gen_types.cpp) is the whole soundness
argument for `noalias`+`readonly` on a shared `&T`. Its IR gate,
tests/logos/ir/param_attrs_freeze_lattice, used to pin FIVE axes against a
predicate that makes SEVENTEEN separate verdicts. That gap was not theoretical:
replacing the element recursion in the TUPLE arm with `return true` moved
`&(Cell<i64>, i64)` from `ptr noundef align 8` to `ptr noalias noundef readonly
align 8` — a real noalias+readonly on an interior-mutable pointee — with all
five axes AND L2 (1881/1881) green.

WRITING A LONGER LIST OF AXES WOULD HAVE BEEN THE SAME DEFECT AGAIN. So this
gate does not hold a list. It DERIVES two populations from the artifacts and
holds them against a ledger in both directions:

  1. THE KIND POPULATION, from include/logos/compiler/sema.hpp. Every
     `LogosType::Kind` is either NAMED by a `case K::…` label in the predicate
     or falls to its `default:` arm. The gate asserts the arithmetic
     `|all| == |named| + |defaulted|` and holds both sets. Adding a Kind to
     sema.hpp without deciding what the predicate should answer for it is red.

  2. THE VERDICT-SITE POPULATION, from the predicate's own body: every
     `return …;` and every assignment of a boolean literal to the result
     variable. Each site must name an AXIS that exists as an `; AXIS <id>`
     marker with real CHECK lines in the lattice .check — or be recorded as
     NO-AXIS with a stated reason. Adding an arm with no axis is red.

  Plus the reverse direction: an `; AXIS` marker in the .check that no ledger
  row claims is red too, so an axis cannot be quietly orphaned by a rename.

⚠ WHAT THIS GATE IS NOT. It does not decide whether an axis is a GOOD axis —
that an axis exists and pins a parameter list is all it can see. The .check
lines themselves are the measurement; this gate only makes sure none of the
predicate's decisions is standing outside them.

⚠ AND IT PROVES ITS OWN INSTRUMENT FIRST. Every reader below is re-run on
planted inputs whose answer is known, through the SAME code path. A gate that
cannot fail certifies nothing.
"""

import os
import re
import sys

BROKEN = 4


class Broken(Exception):
    pass


# ── READERS ──────────────────────────────────────────────────────────────────
# Used on the tree AND on the canaries. The only code that reads source.


def strip_cpp_comments(text):
    """Blank out // and /* */, preserving byte offsets; literal-aware."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in '"\'':
            q, i = c, i + 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = out[i + 1] = " "
                i += 2
            continue
        i += 1
    return "".join(out)


def brace_body(src, start):
    """The text between the first `{` at/after `start` and its match."""
    i = src.find("{", start)
    if i < 0:
        raise Broken("no `{` after offset %d" % start)
    depth, j = 0, i
    while j < len(src):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i + 1: j]
        j += 1
    raise Broken("unbalanced braces from offset %d" % i)


KIND_ENUM_RE = re.compile(r"\benum\s+class\s+Kind\s*\{")


def kind_names(header_text):
    """Every enumerator of `enum class Kind`, in declaration order."""
    src = strip_cpp_comments(header_text)
    m = KIND_ENUM_RE.search(src)
    if not m:
        raise Broken("no `enum class Kind {` in the header")
    body = brace_body(src, m.start())
    names = []
    for part in body.split(","):
        p = part.strip()
        if not p:
            continue
        mm = re.match(r"([A-Za-z_]\w*)", p)
        if mm:
            names.append(mm.group(1))
    if not names:
        raise Broken("`enum class Kind` parsed to zero enumerators")
    return names


FN_RE = re.compile(r"\bbool\s+MLIRGenImpl::type_is_freeze\s*\(")
CASE_RE = re.compile(r"\bcase\s+K::([A-Za-z_]\w*)\s*:")
VERDICT_RE = re.compile(
    r"(?:^|[;{}):\s])("
    r"return\b[^;]*;"                       # any return
    r"|(?:const\s+)?(?:bool\s+)?[A-Za-z_]\w*\s*=\s*(?:true|false)\s*;"   # result bool
    r")"
)


def predicate_body(cpp_text):
    src = strip_cpp_comments(cpp_text)
    m = FN_RE.search(src)
    if not m:
        raise Broken("no `bool MLIRGenImpl::type_is_freeze(` in the source")
    return brace_body(src, m.end())


def norm(s):
    return " ".join(s.split())


def verdict_sites(body):
    """[(ordinal, normalised-text)] for every decision the predicate makes."""
    return [(i + 1, norm(m.group(1)))
            for i, m in enumerate(VERDICT_RE.finditer(body))]


def named_kinds(body):
    """kind -> ordinal of the FIRST verdict site at or after its `case` label."""
    sites = [(m.start(), i + 1) for i, m in enumerate(VERDICT_RE.finditer(body))]
    out = {}
    for m in CASE_RE.finditer(body):
        nxt = next((ordv for off, ordv in sites if off >= m.end()), None)
        if nxt is None:
            raise Broken("`case K::%s` has no verdict site after it"
                         % m.group(1))
        if m.group(1) in out:
            raise Broken("`case K::%s` appears twice in the predicate"
                         % m.group(1))
        out[m.group(1)] = nxt
    return out


AXIS_RE = re.compile(r"^\s*;\s*AXIS\s+(\S+)\s*$")
CHECK_RE = re.compile(r"^\s*;\s*CHECK(?:-DAG|-NEXT|-SAME|-NOT)?\s*:")


def check_axes(check_text):
    """axis-id -> number of CHECK lines under its marker."""
    out, cur = {}, None
    for line in check_text.splitlines():
        m = AXIS_RE.match(line)
        if m:
            if m.group(1) in out:
                raise Broken("axis marker %r appears twice in the .check"
                             % m.group(1))
            cur = m.group(1)
            out[cur] = 0
            continue
        if CHECK_RE.match(line):
            if cur is not None:
                out[cur] += 1
            continue
        if line.strip() and not line.lstrip().startswith(";"):
            cur = None
    return out


# ── LEDGER ───────────────────────────────────────────────────────────────────


def parse_ledger(text):
    """Sectioned ledger -> {section: [row, …]}; `#`/blank lines dropped."""
    sections, cur = {}, None
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        if re.match(r"^[A-Z_]+$", raw.strip()):
            cur = raw.strip()
            sections.setdefault(cur, [])
            continue
        if cur is None:
            raise Broken("ledger row before any section header: %r" % raw)
        sections[cur].append(raw.rstrip("\n"))
    return sections


# ── THE CHECK ────────────────────────────────────────────────────────────────


def run(repo, ledger_path, check_path, hdr_path=None, cpp_path=None):
    """Returns a list of failure strings (empty == green)."""
    hdr_path = hdr_path or os.path.join(repo, "include/logos/compiler/sema.hpp")
    cpp_path = cpp_path or os.path.join(repo, "src/compiler/mlir_gen_types.cpp")
    fails = []

    kinds = kind_names(open(hdr_path, encoding="utf-8").read())
    body = predicate_body(open(cpp_path, encoding="utf-8").read())
    sites = verdict_sites(body)
    named = named_kinds(body)
    axes = check_axes(open(check_path, encoding="utf-8").read())
    led = parse_ledger(open(ledger_path, encoding="utf-8").read())

    for req in ("KINDS_TOTAL", "DEFAULTED", "NAMED", "ARMS", "EXTRA_AXES"):
        if req not in led:
            raise Broken("ledger has no `%s` section" % req)

    # ── 1. THE ARITHMETIC. Every Kind is named or defaulted, nothing else. ──
    unknown = sorted(set(named) - set(kinds))
    if unknown:
        fails.append(
            "the predicate names Kinds that `enum class Kind` does not declare: "
            + ", ".join(unknown) +
            "\n      Either the enumerator was renamed and the predicate was not, or the\n"
            "      gate is reading the wrong header. Both make the coverage arithmetic\n"
            "      below meaningless.")
    defaulted = [k for k in kinds if k not in named]
    want_total = led["KINDS_TOTAL"][0].strip() if led["KINDS_TOTAL"] else ""
    if want_total != str(len(kinds)):
        fails.append(
            "KINDS_TOTAL says %s, `enum class Kind` declares %d.\n"
            "      A NEW Kind is not automatically wrong — but it arrives with NO decision\n"
            "      about what `type_is_freeze` should answer for it, and the `default:` arm\n"
            "      answers `false` silently. Say which arm it belongs to and move the number."
            % (want_total or "(nothing)", len(kinds)))
    if len(kinds) != len(named) + len(defaulted):
        fails.append("a Kind is both named and defaulted — the census is not a partition.")

    got_def = "\n".join(sorted(defaulted))
    want_def = "\n".join(sorted(r.strip() for r in led["DEFAULTED"]))
    if got_def != want_def:
        fails.append(
            "the DEFAULTED set does not match the ledger.\n"
            "      These Kinds reach `default: return false` — conservative, so they cost an\n"
            "      optimization and never soundness. Moving one INTO an arm, or a new Kind\n"
            "      falling in here, is a decision; this is where it gets recorded.\n"
            + _diff(want_def, got_def))

    got_named = "\n".join("%s\t%d" % (k, v) for k, v in sorted(named.items()))
    want_named = "\n".join(r.strip() for r in sorted(led["NAMED"]))
    if got_named != want_named:
        fails.append(
            "the NAMED kind→arm map does not match the ledger.\n"
            "      A Kind that moved from one arm to another has changed the compiler's\n"
            "      answer for every value of that kind.\n"
            + _diff(want_named, got_named))

    # ── 2. THE VERDICT SITES. One axis per decision, or a stated reason. ────
    led_arms = []
    for row in led["ARMS"]:
        parts = row.split("\t")
        if len(parts) != 4:
            raise Broken("ARMS row is not 4 tab-separated fields: %r" % row)
        led_arms.append(parts)

    got_sites = "\n".join("%d\t%s" % (o, t) for o, t in sites)
    want_sites = "\n".join("%s\t%s" % (p[0], p[3]) for p in led_arms)
    if got_sites != want_sites:
        fails.append(
            "the VERDICT SITES of `type_is_freeze` do not match the ledger.\n"
            "      Every `return` and every assignment to the result bool is a decision the\n"
            "      predicate makes, and each one needs an axis in the IR lattice or a stated\n"
            "      reason why it cannot have one. A NEW site with no axis is precisely the\n"
            "      hole that let the tuple arm hand `&(Cell<i64>, i64)` a noalias+readonly\n"
            "      with every gate green.\n"
            + _diff(want_sites, got_sites))

    # ── 3. EVERY CLAIMED AXIS EXISTS AND PINS SOMETHING ─────────────────────
    claimed, no_axis = set(), []
    for ordv, arm_id, axis, _txt in led_arms:
        if axis.startswith("NO-AXIS:"):
            reason = axis.split(":", 1)[1]
            if not reason.strip():
                fails.append("arm %s (%s) is NO-AXIS with no reason given."
                             % (ordv, arm_id))
            no_axis.append((ordv, arm_id, reason))
            continue
        for a in axis.split(","):
            a = a.strip()
            claimed.add(a)
            if a not in axes:
                fails.append(
                    "arm %s (%s) claims axis `%s`, which has no `; AXIS %s` marker in\n"
                    "      %s. An arm whose axis does not exist is an arm nobody is checking."
                    % (ordv, arm_id, a, a, os.path.basename(check_path)))
            elif axes[a] == 0:
                fails.append(
                    "axis `%s` has a marker but NO CHECK line under it. A marker with no\n"
                    "      assertion is decoration." % a)

    for row in led["EXTRA_AXES"]:
        a = row.split("\t")[0].strip()
        claimed.add(a)
        if a not in axes:
            fails.append("EXTRA_AXES claims axis `%s`, which has no marker in %s."
                         % (a, os.path.basename(check_path)))

    orphan = sorted(set(axes) - claimed)
    if orphan:
        fails.append(
            "these `; AXIS` markers exist in %s but no ledger row claims them: %s\n"
            "      An orphaned axis is one a rename silently detached from the arm it was\n"
            "      written for — it still passes, and it no longer means anything."
            % (os.path.basename(check_path), ", ".join(orphan)))

    return fails, sites, kinds, named, defaulted, no_axis, axes


def _diff(want, got):
    import difflib
    out = []
    for line in difflib.unified_diff(want.splitlines(), got.splitlines(),
                                     "ledger", "derived", lineterm="", n=0):
        if line.startswith("---") or line.startswith("+++") or line.startswith("@@"):
            continue
        out.append(("        ledger only: " if line.startswith("-")
                    else "        derived:     ") + line[1:])
    return "\n".join(out)


# ── SELF-CANARIES ────────────────────────────────────────────────────────────
# Every reader above, on planted input whose answer is known. Whatever blinds
# the real measurement blinds these, because it is the same code.

CANARY_HDR = """
struct LogosType {
    enum class Kind {
        Void,   // nothing
        I64,
        Struct,
        Error
    };
};
"""

CANARY_CPP = """
bool MLIRGenImpl::type_is_freeze(TypeRef t, std::unordered_set<std::string>& s) {
    using K = LogosType::Kind;
    if (!t) return false;
    switch (t.kind()) {
    case K::Void: case K::I64:
        return true;   // "return false;" in a comment must not count
    case K::Struct: {
        bool frozen = true;
        if (bad) frozen = false;
        return frozen;
    }
    default: return false;
    }
}
"""

CANARY_CHECK = """
; AXIS axis_one
; CHECK-DAG: define {{.*}}one(ptr)
; AXIS axis_empty
; a marker with nothing under it
"""


def selftest():
    """Raises Broken with an explanation if any reader is dead."""
    ks = kind_names(CANARY_HDR)
    if ks != ["Void", "I64", "Struct", "Error"]:
        raise Broken("the Kind reader misparsed a planted 4-enumerator enum: %r.\n"
                     "  Its count on sema.hpp would be a number, not a measurement." % ks)

    body = predicate_body(CANARY_CPP)
    sites = [t for _o, t in verdict_sites(body)]
    want = ["return false;", "return true;", "bool frozen = true;",
            "frozen = false;", "return frozen;", "return false;"]
    if sites != want:
        raise Broken(
            "the verdict-site reader misread a planted predicate.\n"
            "  expected %r\n  got      %r\n"
            "  It cannot see the decisions `type_is_freeze` makes, so 'every arm has an\n"
            "  axis' would be a statement about nothing." % (want, sites))

    nk = named_kinds(body)
    if nk != {"Void": 2, "I64": 2, "Struct": 3}:
        raise Broken("the kind→arm reader misgrouped a planted switch: %r" % nk)
    if "Error" in nk:
        raise Broken("the kind→arm reader claims a `default:`-only Kind is named.")

    ax = check_axes(CANARY_CHECK)
    if ax != {"axis_one": 1, "axis_empty": 0}:
        raise Broken("the axis-marker reader misread a planted .check: %r.\n"
                     "  A marker with no CHECK line under it must be visible as zero." % ax)

    secs = parse_ledger("# c\nARMS\n1\ta\tb\tc\nEXTRA_AXES\nx\ty\n")
    if secs != {"ARMS": ["1\ta\tb\tc"], "EXTRA_AXES": ["x\ty"]}:
        raise Broken("the ledger reader misparsed a planted ledger: %r" % secs)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    repo = os.path.abspath(argv[1])
    here = os.path.dirname(os.path.abspath(__file__))
    ledger = os.path.join(here, "freeze_arms.ledger")
    check = os.path.join(here, "ir", "param_attrs_freeze_lattice.check")
    args = argv[2:]
    while args:
        if args[0] == "--ledger":
            ledger, args = args[1], args[2:]
        elif args[0] == "--check":
            check, args = args[1], args[2:]
        else:
            print("unknown argument %r" % args[0], file=sys.stderr)
            return 2

    try:
        selftest()
    except Broken as e:
        print("GATE BROKEN (self-canary): %s" % e)
        return BROKEN

    try:
        fails, sites, kinds, named, defaulted, no_axis, axes = \
            run(repo, ledger, check)
    except Broken as e:
        print("GATE BROKEN: %s" % e)
        return BROKEN
    except OSError as e:
        print("GATE BROKEN: %s" % e)
        return BROKEN

    if fails:
        for f in fails:
            print("FAIL: %s" % f)
        return 1

    print("freeze-arm coverage: %d Kinds (%d named across the predicate's arms, "
          "%d left to `default:`);" % (len(kinds), len(named), len(defaulted)))
    print("  %d verdict sites, %d carrying an axis, %d recorded NO-AXIS; "
          "%d axis markers in" % (len(sites), len(sites) - len(no_axis),
                                  len(no_axis), len(axes)))
    print("  the lattice .check, all claimed. Canaries live.")
    if no_axis:
        print("  NO-AXIS arms, stated so the coverage number is not over-read:")
        for ordv, arm_id, reason in no_axis:
            print("    #%-3s %-24s %s" % (ordv, arm_id, reason))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
