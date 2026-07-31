# Copyright 2026 Victor Smirnov
"""THE ENUMERATOR.

Generates Logos programs over the product of the declared AXES, compiles and
runs them, and checks every answer against `model.py` — Python's exact
arithmetic — never against anything Logos computed.

WHY THIS EXISTS. Six rounds of the Deem work shipped with green gates and every
one of them had real defects found afterwards by hand. The diagnosis: coverage
was measured against WHAT THE CORPUS CONTAINS, not against what the type lattice
and the query shapes ADMIT. The corpus is a sample somebody chose; this is the
product nobody chose.

THE GENERATOR IS THE SPEC. Its output is never hand-edited. A case that must be
excluded is excluded HERE, in code, with its ground written next to it — see
`model.oracle_arith` for the four such exclusions.

PROTOCOL. A generated program prints one line per case:
    #<case-id>|<value> [<value> …]
and `DONE <n>` last. A missing `DONE` is a crash and is reported as one; a
missing case line is reported as one. Values leave Logos through the channels
documented in `emit.py`, which are part of the trusted base and are named there.

BATCHING AND ATTRIBUTION. Cases are batched into programs because a compile is
~1.7 s and a case is ~0 s. A program that fails to COMPILE is bisected down to
the single case whose diagnostic is then recorded — so batching costs nothing in
attribution. A program that CRASHES is likewise bisected.

USAGE
    python3 tests/exhaustive/harness.py --list
    python3 tests/exhaustive/harness.py --family cast --jobs 12
    python3 tests/exhaustive/harness.py --all --jobs 12 --json out.json
"""

import argparse
import concurrent.futures as cf
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model
import emit

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOGOSC = os.path.join(REPO, "build", "bin", "logosc")
LIBDIR = os.path.join(REPO, "build", "lib", "logos")


# ── a case and a program ─────────────────────────────────────────────────────
class Case:
    __slots__ = ("cid", "family", "desc", "types", "check", "want")

    def __init__(self, cid, family, desc, types, check, want):
        self.cid = cid          # unique string
        self.family = family
        self.desc = desc        # human-readable, carries the axis coordinates
        self.types = types      # tuple of scalar types on the printed line
        self.check = check      # (parsed_values) -> None | reason-string
        self.want = want        # oracle answer, for the report


# A program is rebuilt from its case list, so bisection is "generate again with
# half the cases" rather than "cut the text in half" — the text is never edited.
class Rebuildable:
    def __init__(self, name, family, cases, build):
        self.name = name
        self.family = family
        self.cases = cases
        self.build = build      # (name, cases) -> source text

    def source(self):
        return self.build(self.name, self.cases)

    def halves(self):
        n = len(self.cases)
        if n <= 1:
            return None
        a, b = self.cases[: n // 2], self.cases[n // 2 :]
        return (
            Rebuildable(self.name + "_a", self.family, a, self.build),
            Rebuildable(self.name + "_b", self.family, b, self.build),
        )


# ── program assembly shared by every family ──────────────────────────────────
def build_program(name, cases, uses, items_of, body_of):
    """(name, cases) -> source. `items_of`/`body_of` are per-case emitters."""
    items, body = [], []
    seen = set()
    for c in cases:
        for it in items_of(c):
            if it not in seen:
                seen.add(it)
                items.append(it)
        body.extend(body_of(c))
    head = emit.PRELUDE_COMMON + "".join(u + "\n" for u in uses)
    return (
        head
        + "\n"
        + "\n".join(items)
        + "\n\nfn main() -> i32 {\n"
        + "\n".join(body)
        + f'\n    println_string(&format!("DONE {len(cases)}"));\n'
        + "    return 0i32;\n}\n"
    )


# ═══════════════════════════════════════════════════════════════════════════
# FAMILY: cast — every ordered pair of integer types × every boundary value
# ═══════════════════════════════════════════════════════════════════════════
def family_cast():
    progs = []
    for src in model.INT_TYPES:
        cases = []
        for dst in model.INT_TYPES:
            for v in model.values_of(src):
                want = model.oracle_cast_int(src, dst, v)
                cid = f"cast.{src}.{dst}.{v}"
                cases.append(
                    Case(
                        cid,
                        "cast",
                        f"({v}: {src}) as {dst}",
                        (dst,),
                        _eq_check((want,)),
                        (want,),
                    )
                )
        progs.append(
            Rebuildable(
                f"cast_{src}",
                "cast",
                cases,
                lambda n, cs: build_program(n, cs, [], lambda c: [], _cast_body),
            )
        )
    return progs


def _cast_body(c):
    _, src, dst, v = c.cid.split(".")
    v = int(v)
    return [
        "    {",
        f"        let s: {src} = {emit.int_literal(src, v)};",
        f"        let d: {dst} = s as {dst};",
        emit.emit_print(c.cid, dst, "d"),
        "    }",
    ]


# ═══════════════════════════════════════════════════════════════════════════
# FAMILY: cmp — six relational operators over all ordered pairs of boundaries
# ═══════════════════════════════════════════════════════════════════════════
CMP_OPS = ("<", "<=", ">", ">=", "==", "!=")
OPNAME = {"<": "lt", "<=": "le", ">": "gt", ">=": "ge", "==": "eq", "!=": "ne"}
NAMEOP = {v: k for k, v in OPNAME.items()}


def family_cmp():
    progs = []
    for t in model.INT_TYPES:
        cases = []
        vs = model.small_values_of(t)
        for a in vs:
            for b in vs:
                for op in CMP_OPS:
                    want = model.oracle_cmp(op, a, b)
                    cid = f"cmp.{t}.{OPNAME[op]}.{a}.{b}"
                    cases.append(
                        Case(
                            cid,
                            "cmp",
                            f"({a}: {t}) {op} ({b}: {t})",
                            ("bool",),
                            _eq_check((want,)),
                            (want,),
                        )
                    )
        progs.append(
            Rebuildable(
                f"cmp_{t}",
                "cmp",
                cases,
                lambda n, cs: build_program(n, cs, [], lambda c: [], _cmp_body),
            )
        )
    return progs


def _cmp_body(c):
    _, t, opn, a, b = c.cid.split(".")
    op = NAMEOP[opn]
    return [
        "    {",
        f"        let x: {t} = {emit.int_literal(t, int(a))};",
        f"        let y: {t} = {emit.int_literal(t, int(b))};",
        f"        let r: bool = x {op} y;",
        emit.emit_print(c.cid, "bool", "r"),
        "    }",
    ]


# ═══════════════════════════════════════════════════════════════════════════
# FAMILY: arith — + - * / % & | ^ and the two shifts, at the boundaries
# ═══════════════════════════════════════════════════════════════════════════
ARITH_OPS = ("+", "-", "*", "/", "%", "&", "|", "^")
AOPNAME = {"+": "add", "-": "sub", "*": "mul", "/": "div", "%": "rem",
           "&": "and", "|": "or", "^": "xor", "<<": "shl", ">>": "shr"}
ANAMEOP = {v: k for k, v in AOPNAME.items()}


def family_arith():
    progs = []
    for t in model.INT_TYPES:
        cases = []
        vs = model.small_values_of(t)
        for a in vs:
            for b in vs:
                for op in ARITH_OPS:
                    want = model.oracle_arith(t, op, a, b)
                    if want is None:
                        continue
                    cid = f"arith.{t}.{AOPNAME[op]}.{a}.{b}"
                    cases.append(
                        Case(cid, "arith", f"({a}: {t}) {op} ({b}: {t})",
                             (t,), _eq_check((want,)), (want,))
                    )
        nb = model.bits_of(t)
        for a in vs:
            for k in sorted({0, 1, nb // 2, nb - 1}):
                for op in ("<<", ">>"):
                    want = model.oracle_shift(t, op, a, k)
                    if want is None:
                        continue
                    cid = f"arith.{t}.{AOPNAME[op]}.{a}.{k}"
                    cases.append(
                        Case(cid, "arith", f"({a}: {t}) {op} {k}",
                             (t,), _eq_check((want,)), (want,))
                    )
        progs.append(
            Rebuildable(
                f"arith_{t}",
                "arith",
                cases,
                lambda n, cs: build_program(n, cs, [], lambda c: [], _arith_body),
            )
        )
    return progs


def _arith_body(c):
    _, t, opn, a, b = c.cid.split(".")
    op = ANAMEOP[opn]
    if op in ("<<", ">>"):
        rhs = f"{int(b)}i32"
    else:
        rhs = emit.int_literal(t, int(b))
    return [
        "    {",
        f"        let x: {t} = {emit.int_literal(t, int(a))};",
        f"        let y: {'i32' if op in ('<<', '>>') else t} = {rhs};",
        f"        let r: {t} = x {op} y;",
        emit.emit_print(c.cid, t, "r"),
        "    }",
    ]


# ═══════════════════════════════════════════════════════════════════════════
# FAMILY: pattern — a range pattern at EVERY emission site that admits one
# ═══════════════════════════════════════════════════════════════════════════
# The axis is the SITE, not the pattern: `match`, `if let`, `let … else` and
# `while let` each lower the same `lo..=hi` through their own emission, and a
# census of sites is exactly what a corpus cannot give you.
PAT_SITES = ("match", "iflet", "letelse", "whilelet")


def family_pattern():
    progs = []
    for t in model.INT_TYPES:
        cases = []
        b = model.bits_of(t)
        # Two ranges per type: one strictly inside the type, one anchored at MAX.
        lo1, hi1 = _inner_range(t)
        ranges = [(lo1, hi1), (model.tmax(t) - 1, model.tmax(t)),
                  (model.tmin(t), model.tmin(t) + 1)]
        for (lo, hi) in ranges:
            probes = sorted({lo - 1, lo, (lo + hi) // 2, hi, hi + 1,
                             model.tmin(t), model.tmax(t), 0, 1}
                            & set(range(model.tmin(t), model.tmax(t) + 1))
                            if b <= 8 else
                            {v for v in {lo - 1, lo, (lo + hi) // 2, hi, hi + 1,
                                         model.tmin(t), model.tmax(t), 0, 1}
                             if model.in_range(t, v)})
            for v in probes:
                want = model.oracle_in_range_pat(lo, hi, v)
                for site in PAT_SITES:
                    cid = f"pat.{t}.{site}.{lo}.{hi}.{v}"
                    cases.append(
                        Case(cid, "pattern",
                             f"`{lo}..={hi}` at site `{site}` on ({v}: {t})",
                             ("bool",), _eq_check((want,)), (want,))
                    )
        progs.append(
            Rebuildable(
                f"pat_{t}",
                "pattern",
                cases,
                lambda n, cs: build_program(n, cs, [], _pat_items, _pat_body),
            )
        )
    return progs


def _inner_range(t):
    lo, hi = model.tmin(t), model.tmax(t)
    if model.signed(t):
        return (lo // 2, hi // 2)
    return ((1 << (model.bits_of(t) - 1)) - 50, (1 << (model.bits_of(t) - 1)) + 50) \
        if model.bits_of(t) > 8 else (100, 200)


def _pat_fn_name(c):
    return "pf_" + c.cid.replace(".", "_").replace("-", "n")


def _pat_items(c):
    _, t, site, lo, hi, v = c.cid.split(".")
    L = emit.int_literal(t, int(lo))
    H = emit.int_literal(t, int(hi))
    fn = _pat_fn_name(c)
    if site == "letelse":
        return [f"fn {fn}(x: {t}) -> bool {{ let {L}..={H} = x else {{ return false; }}; return true; }}"]
    if site == "match":
        return [f"fn {fn}(x: {t}) -> bool {{ return match x {{ {L}..={H} => true, _ => false }}; }}"]
    if site == "iflet":
        return [f"fn {fn}(x: {t}) -> bool {{ let mut r: bool = false; if let {L}..={H} = x {{ r = true; }} return r; }}"]
    if site == "whilelet":
        return [f"fn {fn}(x: {t}) -> bool {{ let mut r: bool = false; while let {L}..={H} = x {{ r = true; break; }} return r; }}"]
    raise AssertionError(site)


def _pat_body(c):
    _, t, site, lo, hi, v = c.cid.split(".")
    fn = _pat_fn_name(c)
    return [
        "    {",
        f"        let x: {t} = {emit.int_literal(t, int(v))};",
        f"        let r: bool = {fn}(x);",
        emit.emit_print(c.cid, "bool", "r"),
        "    }",
    ]


# ═══════════════════════════════════════════════════════════════════════════
# FAMILY: deem — the query shapes, crossed with the CONTEXT shape
# ═══════════════════════════════════════════════════════════════════════════
# The context axis is here because a defect that depends on it is invisible to a
# harness that varies only the type: field POSITION in the row struct, and the
# presence of unrelated locals in the CALLER.
DEEM_TYPES = [t for t in model.INT_TYPES] + ["f64", "f32", "bool", "str"]
POSITIONS = (0, 1, 2)
CONTEXTS = ("bare", "padbefore", "padafter")
SHAPES = ("select", "where_lt", "where_le", "where_gt", "where_ge",
          "where_eq", "where_ne", "order_asc", "order_desc", "limit",
          "first", "join", "anti", "proj")


def deem_rows(t):
    """The row corpus for a payload type: (payload, id) in a fixed order."""
    if t in model.INT_TYPES:
        vals = model.values_of(t)
    elif t == "f64":
        vals = [v for n, v in model.F64_VALUES if n not in model.POISON_F64]
    elif t == "f32":
        vals = [model.as_f32(v) for n, v in model.F32_VALUES
                if n not in model.POISON_F32]
    elif t == "bool":
        vals = [True, False, True]
    elif t == "str":
        vals = [v for _, v in model.STR_VALUES]
    else:
        raise AssertionError(t)
    return list(enumerate(vals))     # (id, payload)


def _cmp_key(t, v):
    """The Python key realizing the TYPE's own order. NaN is handled by callers."""
    return v


def family_deem():
    progs = []
    for t in DEEM_TYPES:
        for pos in POSITIONS:
            for ctx in CONTEXTS:
                cases = []
                for shape in SHAPES:
                    c = _deem_case(t, pos, ctx, shape)
                    if c is not None:
                        cases.append(c)
                progs.append(
                    Rebuildable(
                        f"deem_{t}_p{pos}_{ctx}",
                        "deem",
                        cases,
                        lambda n, cs: build_program(
                            n, cs,
                            ["use logos.std.wql.wql;",
                             "use logos.mem.collections.vec;",
                             "use logos.lang.option;"],
                            _deem_items, _deem_body),
                    )
                )
    return progs


def _ordered(t):
    return t in model.INT_TYPES or t in ("f64", "f32", "str")


def _deem_pivot(t):
    rows = deem_rows(t)
    vals = [v for _, v in rows]
    if t in model.INT_TYPES:
        return sorted(vals)[len(vals) // 2]
    if t in ("f64", "f32"):
        return 0.5 if t == "f64" else model.as_f32(0.5)
    if t == "str":
        return "a"
    return True


def _deem_case(t, pos, ctx, shape):
    rows = deem_rows(t)
    cid = f"deem.{t}.{pos}.{ctx}.{shape}"
    desc = f"payload {t} at field {pos}, caller ctx {ctx}, shape {shape}"

    if shape == "select":
        want = [v for _, v in rows]
        return Case(cid, "deem", desc, ("SEQ", t), _seq_check(t, want), want)

    if shape.startswith("where_"):
        op = NAMEOP[shape.split("_")[1]]
        if t == "bool" and op not in ("==", "!="):
            return None
        if t in ("f64", "f32") and op in ("<", "<=", ">", ">="):
            pass  # admitted; NaN comparisons are False under IEEE and Python agrees
        piv = _deem_pivot(t)
        want = []
        for i, v in rows:
            if t in ("f64", "f32"):
                keep = _float_cmp(op, v, piv)
            elif t == "str":
                keep = model.oracle_cmp(op, v, piv)
            elif t == "bool":
                keep = (v == piv) if op == "==" else (v != piv)
            else:
                keep = model.oracle_cmp(op, v, piv)
            if keep:
                want.append(i)
        return Case(cid, "deem", desc + f" (pivot {piv!r})",
                    ("SEQ", "i64"), _seq_check("i64", want), want)

    if shape in ("order_asc", "order_desc"):
        if not _ordered(t):
            return None
        desc_ord = shape == "order_desc"
        return Case(cid, "deem", desc,
                    ("SEQ", "i64"), _order_check(t, rows, desc_ord), "sorted-by-key")

    if shape == "limit":
        want = [i for i, _ in rows][:2]
        return Case(cid, "deem", desc, ("SEQ", "i64"), _seq_check("i64", want), want)

    if shape == "first":
        want = [rows[0][0]]
        return Case(cid, "deem", desc, ("SEQ", "i64"), _seq_check("i64", want), want)

    if shape == "join":
        # ts holds every SECOND id; the join must return exactly those rows.
        keep = [i for i, _ in rows if i % 2 == 0]
        return Case(cid, "deem", desc, ("SEQ", "i64"), _seq_check("i64", keep), keep)

    if shape == "anti":
        keep = [i for i, _ in rows if i % 2 == 1]
        return Case(cid, "deem", desc, ("SEQ", "i64"), _seq_check("i64", keep), keep)

    if shape == "proj":
        want = [v for _, v in rows]
        return Case(cid, "deem", desc, ("SEQ", t), _seq_check(t, want), want)

    raise AssertionError(shape)


def _float_cmp(op, a, b):
    import math
    if math.isnan(a) or math.isnan(b):
        return op == "!="
    return model.oracle_cmp(op, a, b)


def _deem_struct(t, pos):
    fields = []
    for i in range(3):
        fields.append(f"pub f{i}: " + (t if i == pos else "i64"))
    fields.append("pub id: i64")
    return "struct S { " + ", ".join(fields) + " }"


def _qname(c):
    return "dq_" + c.cid.replace(".", "_")


def _deem_query(c):
    _, t, pos, ctx, shape = c.cid.split(".")
    pos = int(pos)
    q = _qname(c)
    f = f"s.f{pos}"
    if shape == "select":
        return f"pub deem {q}(ss: &[S]) {{ from ss s select {f} }}"
    if shape.startswith("where_"):
        op = NAMEOP[shape.split("_")[1]]
        piv = emit.literal(t, _deem_pivot(t))
        return f"pub deem {q}(ss: &[S]) {{ from ss s where {f} {op} {piv} select s.id }}"
    if shape == "order_asc":
        return f"pub deem {q}(ss: &[S]) {{ from ss s select s.id order by {f} }}"
    if shape == "order_desc":
        return f"pub deem {q}(ss: &[S]) {{ from ss s select s.id order by {f} desc }}"
    if shape == "limit":
        return f"pub deem {q}(ss: &[S]) {{ from ss s select s.id limit 2 }}"
    if shape == "first":
        return f"pub deem {q}(ss: &[S]) {{ from ss s select first s.id }}"
    if shape == "join":
        return f"pub deem {q}(ss: &[S], ts: &[T2]) {{ from ss s join ts t on s.id == t.id select s.id }}"
    if shape == "anti":
        return f"pub deem {q}(ss: &[S], ts: &[T2]) {{ from ss s anti join ts t on s.id == t.id select s.id }}"
    if shape == "proj":
        return f"pub deem {q}(ss: &[S]) {{ from ss s where s.id >= 0i64 select {f} }}"
    raise AssertionError(shape)


def _deem_items(c):
    _, t, pos, ctx, shape = c.cid.split(".")
    return [_deem_struct(t, int(pos)),
            "struct T2 { pub id: i64, pub w: i64 }",
            _deem_query(c)]


def _deem_rows_literal(t, pos):
    rows = deem_rows(t)
    out = []
    for i, v in rows:
        fs = []
        for k in range(3):
            fs.append(f"f{k}: " + (emit.literal(t, v) if k == pos
                                   else emit.int_literal("i64", 1000 + 10 * i + k)))
        fs.append(f"id: {i}i64")
        out.append("S { " + ", ".join(fs) + " }")
    return out, len(rows)


def _deem_body(c):
    _, t, pos, ctx, shape = c.cid.split(".")
    pos = int(pos)
    rows, n = _deem_rows_literal(t, pos)
    q = _qname(c)
    L = ["    {"]
    if ctx == "padbefore":
        # An UNRELATED local, present only to move the caller's frame. Defect (b)
        # was visible only with one, so its absence/presence is an AXIS.
        L.append("        let mut padA: [i64; 5] = [11i64, 22i64, 33i64, 44i64, 55i64];")
        L.append("        let mut padB: [u8; 3] = [1u8, 2u8, 3u8];")
    L.append(f"        let ss: [S; {n}] = [" + ", ".join(rows) + "];")
    tn = [i for i, _ in deem_rows(t) if i % 2 == 0]
    L.append(f"        let ts: [T2; {max(1, len(tn))}] = ["
             + ", ".join(f"T2 {{ id: {i}i64, w: 0i64 }}" for i in (tn or [0]))
             + "];")
    if ctx == "padafter":
        L.append("        let mut padA: [i64; 5] = [11i64, 22i64, 33i64, 44i64, 55i64];")
        L.append("        let mut padB: [u8; 3] = [1u8, 2u8, 3u8];")

    if shape == "first":
        # `select first` returns Result<Option<T>>, so the Option is DESTRUCTURED
        # rather than assigned. ⚠ The first draft of this generator wrote
        # `let r: i64 = q(..).unwrap();` — and it COMPILED, yielding a stack
        # address. That is reported separately (an `Option<T>` binding to a `T`
        # slot is accepted); the query family must not carry that defect's noise.
        L.append(f"        match {q}(&ss[..]).unwrap() {{")
        L.append(f"            Option::Some(_v) => {{ println_string(&format!(\"#{c.cid}|1 {{}}\", _v)); }}")
        L.append(f"            Option::None => {{ println_string(&format!(\"#{c.cid}|0\")); }}")
        L.append("        }")
    else:
        elem = t if shape in ("select", "proj") else "i64"
        args = "&ss[..], &ts[..]" if shape in ("join", "anti") else "&ss[..]"
        L.append(f"        let r: Vec<{elem}> = {q}({args}).unwrap();")
        L.append('        let mut _acc: String = String::new();')
        # The line is assembled by an explicit loop so its LENGTH is data, not
        # a number baked into the generator: a short/long answer is then a
        # measured disagreement rather than a formatting crash.
        ph, ex = emit.print_exprs(elem, "r.get(_i)")
        L.append(f'        _acc.push_str(format!("#{c.cid}|{{}}", r.len()).as_str());')
        L.append("        let mut _i: i64 = 0i64;")
        L.append("        while _i < r.len() {")
        L.append(f'            _acc.push_str(format!(" {ph}", {", ".join(ex)}).as_str());')
        L.append("            _i = _i + 1i64;")
        L.append("        }")
        L.append("        println_string(&_acc);")
    if ctx in ("padbefore", "padafter"):
        # The unrelated locals are READ BACK. Defect (b)'s signature is that the
        # query and the caller's frame interfere, so "did the query damage the
        # caller's own data" is a claim worth making, not just "was the answer
        # right".
        L.append('        if padA[0i64] != 11i64 || padB[0i64] != 1u8 {')
        L.append(f'            println_string(&format!("PADCLOBBER {c.cid}"));')
        L.append("        }")
    L.append("    }")
    return L



# ═══════════════════════════════════════════════════════════════════════════
# FAMILY: poison — every value the shared corpora had to exclude, on its own
# ═══════════════════════════════════════════════════════════════════════════
# A value excluded from a corpus must still be MEASURED somewhere, or the
# exclusion quietly becomes coverage loss. One program per excluded value: if
# the underlying defect is fixed, these turn green and the exclusion can go.
def family_poison():
    progs = []
    for ty, names, table in (("f64", model.POISON_F64, model.F64_VALUES),
                             ("f32", model.POISON_F32, model.F32_VALUES)):
        for nm, v in table:
            if nm not in names:
                continue
            val = model.as_f32(v) if ty == "f32" else v
            cid = f"poison.{ty}.{nm}"
            want = emit.expected_scalar(ty, val)
            cases = [Case(cid, "poison", f"a bare `{ty}` literal for {nm}",
                          (ty,), _eq_check((want,)), (want,))]
            progs.append(Rebuildable(
                f"poison_{ty}_{nm}", "poison", cases,
                lambda n, cs: build_program(n, cs, [], lambda c: [], _poison_body)))
    return progs


def _poison_body(c):
    _, ty, nm = c.cid.split(".")
    table = model.F64_VALUES if ty == "f64" else model.F32_VALUES
    v = dict(table)[nm]
    if ty == "f32":
        v = model.as_f32(v)
    return [
        "    {",
        f"        let s: {ty} = {emit.float_literal(ty, v)};",
        emit.emit_print(c.cid, ty, "s"),
        "    }",
    ]


# ── checkers ─────────────────────────────────────────────────────────────────
def _eq_check(want):
    def f(got):
        if list(got) != list(want):
            return f"got {got!r}, want {list(want)!r}"
        return None
    return f


def _seq_check(t, want):
    """First token is the row COUNT; the rest are the values, in order."""
    exp = [emit.expected_scalar(t, v) for v in want]

    def f(got):
        if not got:
            return "no values"
        n = got[0]
        vals = got[1:]
        if n != len(want):
            return f"row count {n}, want {len(want)} (values got {vals!r}, want {exp!r})"
        if list(vals) != exp:
            return f"values {vals!r}, want {exp!r}"
        return None
    return f


def _order_check(t, rows, descending):
    """PROPERTY: the OUTPUT is ordered under the type's own order.

    Not "equals another sort" — that is the anti-pattern this harness replaces.
    Two independent claims, both computed here:
      1. the returned ids are a PERMUTATION of the input ids (nothing lost, nothing
         invented);
      2. reading each id's key from the generator's own row table, the key
         sequence is monotone. Floats: NaN has no place in a total order, so the
         monotone claim is made over the NON-NaN subsequence only — which is
         exactly the claim `order by` on an f64 key is allowed to make, and it is
         still violated by a shift that halts at a NaN.
    """
    import math
    key = {i: v for i, v in rows}
    ids = sorted(key)

    def f(got):
        if not got:
            return "no values"
        n, vals = got[0], list(got[1:])
        if n != len(ids):
            return f"row count {n}, want {len(ids)}"
        if sorted(vals) != ids:
            return f"not a permutation of the input ids: {vals!r}"
        ks = [key[i] for i in vals]
        if t in ("f64", "f32"):
            ks = [k for k in ks if not math.isnan(k)]
        for a, b in zip(ks, ks[1:]):
            if descending and a < b:
                return f"NOT descending at {a!r} < {b!r}; key sequence {ks!r} for ids {vals!r}"
            if not descending and a > b:
                return f"NOT ascending at {a!r} > {b!r}; key sequence {ks!r} for ids {vals!r}"
        return None
    return f


# ── the runner ───────────────────────────────────────────────────────────────
def parse_output(out):
    lines = {}
    done = None
    for ln in out.splitlines():
        if ln.startswith("DONE "):
            done = int(ln.split()[1])
            continue
        if not ln.startswith("#") or "|" not in ln:
            continue
        cid, rest = ln[1:].split("|", 1)
        lines[cid] = rest
    return lines, done


def tokenize(t_list, rest):
    """Split a value line into typed Python values.

    `t_list` is either a tuple of scalar type names, or the pair
    ("SEQ", elem) — a row COUNT followed by that many values of `elem`. The
    sequence form reads its length from the ANSWER, so a short or long answer is
    a measured disagreement rather than a parse crash: the count is data.
    """
    if len(t_list) == 2 and t_list[0] == "SEQ":
        elem = t_list[1]
        q = _split_tokens(rest)
        n = int(q.pop(0))
        vals = [n]
        for _ in range(n):
            if not q:
                break
            vals.append(q.pop(0) if elem == "str" else emit.parse_scalar(elem, q))
        return vals
    q = _split_tokens(rest)
    vals = []
    for t in t_list:
        if t == "str":
            vals.append(q.pop(0))
        else:
            vals.append(emit.parse_scalar(t, q))
    return vals


def _split_tokens(s):
    toks = []
    i = 0
    while i < len(s):
        if s.startswith("[[", i):
            j = s.index("]]", i)
            toks.append(s[i + 2:j])
            i = j + 2
        elif s[i] == " ":
            i += 1
        else:
            j = i
            while j < len(s) and s[j] != " " and not s.startswith("[[", j):
                j += 1
            toks.append(s[i:j])
            i = j
    return toks


def run_program(prog, workdir):
    src = os.path.join(workdir, prog.name + ".logos")
    obj = os.path.join(workdir, prog.name + ".o")
    binp = os.path.join(workdir, prog.name + ".bin")
    with open(src, "w") as f:
        f.write(prog.source())
    cp = subprocess.run([LOGOSC, src, "-o", obj], capture_output=True, text=True)
    if cp.returncode != 0 or not os.path.exists(obj):
        return ("COMPILE_FAIL", cp.stdout + cp.stderr)
    archives = sorted(
        os.path.join(LIBDIR, a) for a in os.listdir(LIBDIR) if a.endswith(".a"))
    link = subprocess.run(
        ["cc", obj, "-Wl,--start-group", *archives, "-Wl,--end-group",
         "-lpthread", "-lm", "-lstdc++", "-Wl,--gc-sections",
         "-Wl,--allow-multiple-definition", "-o", binp],
        capture_output=True, text=True)
    if link.returncode != 0:
        return ("LINK_FAIL", link.stderr[-4000:])
    rp = subprocess.run([binp], capture_output=True, text=True, timeout=120)
    return ("RAN", (rp.returncode, rp.stdout, rp.stderr))


FINDINGS = []


def check_program(prog, workdir):
    kind, payload = run_program(prog, workdir)
    if kind in ("COMPILE_FAIL", "LINK_FAIL"):
        h = prog.halves()
        if h is None:
            c = prog.cases[0]
            FINDINGS.append(dict(kind=kind, cid=c.cid, family=c.family,
                                 desc=c.desc, want=repr(c.want),
                                 got=payload.strip()[-1500:]))
            return
        for p in h:
            check_program(p, workdir)
        return
    rc, out, err = payload
    lines, done = parse_output(out)
    crashed = (done != len(prog.cases))
    if crashed and len(prog.cases) > 1:
        h = prog.halves()
        for p in h:
            check_program(p, workdir)
        return
    for c in prog.cases:
        if c.cid not in lines:
            FINDINGS.append(dict(kind="NO_ANSWER", cid=c.cid, family=c.family,
                                 desc=c.desc, want=repr(c.want),
                                 got=f"program exit {rc}, no line for this case; "
                                     f"stderr {err.strip()[-300:]}"))
            continue
        try:
            vals = tokenize(c.types, lines[c.cid])
        except Exception as e:
            FINDINGS.append(dict(kind="MALFORMED", cid=c.cid, family=c.family,
                                 desc=c.desc, want=repr(c.want),
                                 got=f"{lines[c.cid]!r} ({e})"))
            continue
        reason = c.check(vals)
        if reason:
            FINDINGS.append(dict(kind="WRONG_ANSWER", cid=c.cid, family=c.family,
                                 desc=c.desc, want=repr(c.want), got=reason))


FAMILIES = {
    "cast": family_cast,
    "cmp": family_cmp,
    "arith": family_arith,
    "pattern": family_pattern,
    "deem": family_deem,
    "poison": family_poison,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--family", action="append", default=[])
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--jobs", type=int, default=12)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--json", default=None)
    ap.add_argument("--workdir", default=None)
    ap.add_argument("--only", default=None, help="substring filter on program name")
    args = ap.parse_args()

    fams = list(FAMILIES) if (args.all or not args.family) else args.family
    progs = []
    for f in fams:
        progs.extend(FAMILIES[f]())
    if args.only:
        progs = [p for p in progs if args.only in p.name]

    ncases = sum(len(p.cases) for p in progs)
    if args.list:
        print(f"{len(progs)} programs, {ncases} cases")
        for f in fams:
            ps = [p for p in progs if p.family == f]
            print(f"  {f:8s} {len(ps):4d} programs {sum(len(p.cases) for p in ps):6d} cases")
        return 0

    wd = args.workdir or tempfile.mkdtemp(prefix="logos-exh-")
    os.makedirs(wd, exist_ok=True)
    t0 = time.time()
    print(f"[harness] {len(progs)} programs, {ncases} cases, workdir {wd}", flush=True)
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(check_program, p, wd): p for p in progs}
        n = 0
        for fu in cf.as_completed(futs):
            n += 1
            try:
                fu.result()
            except Exception as e:
                FINDINGS.append(dict(kind="HARNESS_ERROR", cid=futs[fu].name,
                                     family=futs[fu].family, desc="", want="",
                                     got=repr(e)))
            if n % 25 == 0:
                print(f"[harness] {n}/{len(progs)} programs, "
                      f"{len(FINDINGS)} findings, {time.time()-t0:.0f}s", flush=True)
    dt = time.time() - t0
    print(f"[harness] DONE {len(progs)} programs, {ncases} cases, "
          f"{len(FINDINGS)} findings, {dt:.0f}s", flush=True)
    by = {}
    for f in FINDINGS:
        by[(f["family"], f["kind"])] = by.get((f["family"], f["kind"]), 0) + 1
    for k in sorted(by):
        print(f"  {k[0]:8s} {k[1]:14s} {by[k]}")
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(dict(programs=len(progs), cases=ncases, seconds=dt,
                           findings=FINDINGS), fh, indent=1)
        print(f"[harness] findings -> {args.json}")
    return 1 if FINDINGS else 0


if __name__ == "__main__":
    sys.exit(main())
