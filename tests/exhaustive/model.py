# Copyright 2026 Victor Smirnov
"""THE ORACLE. Nothing in this file may be derived from what Logos computes.

Every expected answer the harness checks is produced here, from Python's exact
integer arithmetic and the IEEE-754 semantics of the `struct` module. The rule
that makes this harness worth running: an expectation is INDEPENDENT of the
implementation under test — it does not share code, algorithm or assumption
with it. A sort written in Logos with the emitter's own comparator is NOT an
oracle; it reproduces the defect and then agrees with it.

WHAT IS TRUSTED, stated so it can be argued with:
  * Python's `int` (unbounded, exact) and `struct.pack` (the platform's IEEE-754).
  * The DECLARED semantics of Logos: two's complement integers of the declared
    width, Rust-shaped `as` (truncate/sign-extend, never trap), `/` and `%`
    truncating toward zero.
  * The RENDER BRIDGE (`render.py` docs below) for the types with no `Display`.
Everything else is measured.
"""

import struct

# ── the integer lattice ──────────────────────────────────────────────────────
# (bits, signed). The odd widths are this project's own; they are in the lattice
# because they are in the language, not because a test happened to use them.
INT_TYPES = {
    "i8":    (8,   True),
    "u8":    (8,   False),
    "i16":   (16,  True),
    "u16":   (16,  False),
    "i24":   (24,  True),
    "u24":   (24,  False),
    "i32":   (32,  True),
    "u32":   (32,  False),
    "i56":   (56,  True),
    "u56":   (56,  False),
    "i64":   (64,  True),
    "u64":   (64,  False),
    "i128":  (128, True),
    "u128":  (128, False),
    "isize": (64,  True),
    "usize": (64,  False),
}

FLOAT_TYPES = ("f32", "f64")
SCALAR_TYPES = tuple(INT_TYPES) + FLOAT_TYPES + ("bool", "str")


def bits_of(t):
    return INT_TYPES[t][0]


def signed(t):
    return INT_TYPES[t][1]


def tmin(t):
    b, s = INT_TYPES[t]
    return -(1 << (b - 1)) if s else 0


def tmax(t):
    b, s = INT_TYPES[t]
    return (1 << (b - 1)) - 1 if s else (1 << b) - 1


def in_range(t, v):
    return tmin(t) <= v <= tmax(t)


def wrap(t, v):
    """Two's-complement reinterpretation of `v` into `t`. This is `as`."""
    b, s = INT_TYPES[t]
    u = v & ((1 << b) - 1)
    if s and u >= (1 << (b - 1)):
        u -= 1 << b
    return u


# ── VALUE axis: boundaries, never round numbers ──────────────────────────────
def values_of(t):
    """Every boundary the brief names, deduplicated, sorted, in range.

    For an unsigned width the SIGNED CEILING and its neighbours are the point:
    2^(b-1)-1 / 2^(b-1) / 2^(b-1)+1 is where a value reinterpreted as signed
    changes sign, which is how an unsigned compare emitted as a signed one is
    caught. For a signed width MIN is the point: -MIN is not representable.
    """
    b, s = INT_TYPES[t]
    lo, hi = tmin(t), tmax(t)
    vs = {0, 1, lo, lo + 1, hi, hi - 1}
    if s:
        vs |= {-1, -(1 << (b - 2)), 1 << (b - 2)}
    else:
        half = 1 << (b - 1)               # 2^(bits-1) exactly
        vs |= {half - 1, half, half + 1}
    return sorted(v for v in vs if lo <= v <= hi)


def small_values_of(t):
    """A 6-wide deterministic subset for the quadratic families (all pairs).

    RULE, stated because a sampling rule that is not stated is a silent
    truncation: keep MIN, MIN+1, 0, 1, MAX-1, MAX for a signed type, and
    0, 1, 2^(b-1)-1, 2^(b-1), MAX-1, MAX for an unsigned one — i.e. both ends
    plus the sign-reinterpretation boundary. Never a midpoint, never random.
    """
    b, s = INT_TYPES[t]
    if s:
        vs = [tmin(t), tmin(t) + 1, 0, 1, tmax(t) - 1, tmax(t)]
    else:
        half = 1 << (b - 1)
        vs = [0, 1, half - 1, half, tmax(t) - 1, tmax(t)]
    out = []
    for v in vs:
        if v not in out and in_range(t, v):
            out.append(v)
    return out


# ── float values ─────────────────────────────────────────────────────────────
F64_VALUES = [
    ("zero",      0.0),
    ("negzero",   -0.0),
    ("one",       1.0),
    ("negone",    -1.0),
    ("nan",       float("nan")),
    ("inf",       float("inf")),
    ("neginf",    float("-inf")),
    ("subnormal", 5e-324),
    ("maxfin",    1.7976931348623157e308),
    ("minfin",    -1.7976931348623157e308),
    ("half",      0.5),
]

F32_VALUES = [
    ("zero",      0.0),
    ("negzero",   -0.0),
    ("one",       1.0),
    ("negone",    -1.0),
    ("nan",       float("nan")),
    ("inf",       float("inf")),
    ("neginf",    float("-inf")),
    ("subnormal", struct.unpack("<f", struct.pack("<I", 1))[0]),
    ("maxfin",    struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]),
    ("half",      0.5),
]

STR_VALUES = [
    ("empty",  ""),
    ("one",    "a"),
    ("utf8",   "é中\U0001f600"),
    ("quote",  'q"q'),
    ("bslash", "b\\b"),
    ("long",   "L" * 137),
]


# ── POISON: values whose mere PRESENCE in a program stops the compiler ───────
# An exception lives in the generator with its ground, never in its output. Each
# entry names a MEASURED finding; removing the entry must make the finding
# reappear. A poisoned value is still exercised — by its own one-value case in
# the `crash` family — it is only kept out of the shared row corpora, where one
# uncompilable literal would mask every other claim in the same program.
POISON_F64 = {
    # `logosc` aborts with an uncaught std::out_of_range from `std::stod` on any
    # SUBNORMAL f64 literal. Measured on `let x: f64 = 5e-324f64;` with no deem
    # anywhere in the file; 2.2250738585072014e-308 (DBL_MIN) compiles, 1e-310
    # and 1e-320 abort. FINDING H1.
    "subnormal",
}
POISON_F32 = {
    "subnormal",   # same abort — the f32 subnormal's decimal is a subnormal f64
}


def f64_bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def f32_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def as_f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


# ── ORACLES ──────────────────────────────────────────────────────────────────
def oracle_cast_int(src, dst, v):
    """`v as dst` where v: src. Rust-shaped: truncate low bits, reinterpret."""
    return wrap(dst, v)


def oracle_cmp(op, a, b):
    """A comparison over Python ints — no width, no signedness, no emission."""
    return {
        "<":  a < b,
        "<=": a <= b,
        ">":  a > b,
        ">=": a >= b,
        "==": a == b,
        "!=": a != b,
    }[op]


def oracle_arith(t, op, a, b):
    """Exact result, or None when the case is EXCLUDED (with its ground).

    Excluded, and this is the generator's own exception rather than a hand edit
    of its output: (1) a result outside the type — Logos's overflow behaviour is
    a separate question this family does not ask; (2) division or remainder by
    zero — a trap, not a value; (3) MIN / -1 — the one signed division that has
    no representable result; (4) a shift amount at or above the width.
    """
    if op == "+":
        r = a + b
    elif op == "-":
        r = a - b
    elif op == "*":
        r = a * b
    elif op == "/":
        if b == 0:
            return None
        if signed(t) and a == tmin(t) and b == -1:
            return None
        r = abs(a) // abs(b)
        if (a < 0) != (b < 0):
            r = -r
    elif op == "%":
        if b == 0:
            return None
        if signed(t) and a == tmin(t) and b == -1:
            return None
        r = abs(a) % abs(b)
        if a < 0:
            r = -r
    elif op == "&":
        r = _bitop(t, a, b, lambda x, y: x & y)
    elif op == "|":
        r = _bitop(t, a, b, lambda x, y: x | y)
    elif op == "^":
        r = _bitop(t, a, b, lambda x, y: x ^ y)
    else:
        raise AssertionError(op)
    return r if in_range(t, r) else None


def _bitop(t, a, b, f):
    m = (1 << bits_of(t)) - 1
    return wrap(t, f(a & m, b & m))


def oracle_shift(t, op, a, k):
    b = bits_of(t)
    if k < 0 or k >= b:
        return None
    if op == "<<":
        r = wrap(t, (a & ((1 << b) - 1)) << k)
        # Only assert the cases where nothing left the type: a shift that drops
        # bits is a question about overflow, which this family does not ask.
        return r if (a << k) == r else None
    if op == ">>":
        if signed(t):
            return a >> k                    # arithmetic, Python's own
        return (a & ((1 << b) - 1)) >> k     # logical
    raise AssertionError(op)


def oracle_in_range_pat(lo, hi, v):
    """`lo..=hi` membership under the TYPE's own order — a Python comparison."""
    return lo <= v <= hi
