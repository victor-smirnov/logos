# Copyright 2026 Victor Smirnov
"""THE RENDER BRIDGE — how a Logos value reaches Python, and what that costs.

The harness never asks Logos whether an answer is right. It asks Logos to SAY
what it computed, and Python decides. So every scalar type needs a channel out,
and the channel is part of the trusted base — it must be named, not assumed.

CHANNELS, and the trust each one costs:
  * `Display` types (i8 u8 i16 u16 i32 u32 i64 u64 isize usize bool): printed by
    `format!("{}")`. Trusts integer rendering only.
  * i24 u24 i56 u56: NO `Display` impl exists (measured — see FINDINGS), so they
    go out through ONE value-preserving widening `as i64` / `as u64`. That
    widening is itself under test in the cast family, over a channel that does
    not use it (i64/u64 are Display types), so the bridge is checked before it
    is relied on.
  * i128 u128: no `Display` either; split into two u64 halves by `>> 64` and
    `as u64`, reassembled here. Trusts a shift and a truncating cast, both
    measured in the cast/shift families over Display types first.
  * f32 f64: `{}` is `%g` and LOSES BITS (measured: 3.4028235e38f32 prints as
    "3.40282e+38"), so floats go out as their IEEE bit pattern via
    `logos.lang.num.float::{f32,f64}_to_bits`. Exact, and it makes -0.0 and NaN
    payloads visible, which `%g` does not.
  * str: printed between `[[` and `]]`. The generated corpus contains no value
    with `]]` in it (asserted in `str_literal`).
"""

import model

DISPLAY_INT = {"i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "isize", "usize"}
BRIDGE_I64 = {"i24", "i56"}
BRIDGE_U64 = {"u24", "u56"}
WIDE = {"i128", "u128"}

PRELUDE_COMMON = """package exh;

use logos.std.io;
use logos.std.fmt;
use logos.mem.string;
use logos.lang.str;
use logos.lang.num.float;
"""


def int_literal(t, v):
    return f"{v}{t}"


def float_literal(ty, x):
    import math
    suf = ty
    if math.isnan(x):
        return f"(0.0{suf} / 0.0{suf})"
    if math.isinf(x):
        return f"({'-' if x < 0 else ''}1.0{suf} / 0.0{suf})"
    if x == 0.0 and math.copysign(1.0, x) < 0:
        return f"(-0.0{suf})"
    # repr() round-trips an f64 exactly; for f32 the value has already been
    # rounded to f32 by the model, so its repr parses back to the same f32.
    r = repr(float(x))
    if "e" in r or "E" in r:
        m, e = r.split("e")
        if "." not in m:
            m += ".0"
        return f"({m}{suf}e{e})" if False else f"({m}e{e}{suf})"
    if "." not in r:
        r += ".0"
    return f"({r}{suf})"


def str_literal(s):
    assert "]]" not in s, "corpus string would collide with the [[ ]] delimiter"
    assert "\n" not in s, "corpus string would collide with the line protocol"
    out = s.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + out + '"'


def literal(t, v):
    if t in model.INT_TYPES:
        return int_literal(t, v)
    if t in model.FLOAT_TYPES:
        return float_literal(t, v)
    if t == "bool":
        return "true" if v else "false"
    if t == "str":
        return str_literal(v)
    raise AssertionError(t)


def print_exprs(t, expr):
    """(fmt_placeholders, [logos expressions]) that carry a `t`-valued expr out."""
    if t in DISPLAY_INT or t == "bool":
        return "{}", [expr]
    if t in BRIDGE_I64:
        return "{}", [f"(({expr}) as i64)"]
    if t in BRIDGE_U64:
        return "{}", [f"(({expr}) as u64)"]
    if t in WIDE:
        return "{} {}", [f"((({expr}) >> 64i32) as u64)", f"(({expr}) as u64)"]
    if t == "f64":
        return "{}", [f"f64_to_bits({expr})"]
    if t == "f32":
        return "{}", [f"f32_to_bits({expr})"]
    if t == "str":
        return "[[{}]]", [expr]
    raise AssertionError(t)


def emit_print(case_id, t, expr):
    ph, es = print_exprs(t, expr)
    args = ", ".join(es)
    return f'    println_string(&format!("#{case_id}|{ph}", {args}));'


def emit_print_many(case_id, parts):
    """parts: [(type, expr)] — one line carrying several values."""
    phs, es = [], []
    for t, e in parts:
        ph, ex = print_exprs(t, e)
        phs.append(ph)
        es.extend(ex)
    args = ", ".join(es)
    if args:
        return f'    println_string(&format!("#{case_id}|{" ".join(phs)}", {args}));'
    return f'    println_string(&format!("#{case_id}|"));'


# ── parsing the other direction ──────────────────────────────────────────────
def parse_scalar(t, toks):
    """Consume tokens from `toks` (a list, front-first) and return a Python value."""
    if t in DISPLAY_INT:
        return int(toks.pop(0))
    if t == "bool":
        v = toks.pop(0)
        assert v in ("true", "false"), v
        return v == "true"
    if t in BRIDGE_I64 or t in BRIDGE_U64:
        return int(toks.pop(0))
    if t in WIDE:
        hi = int(toks.pop(0))
        lo = int(toks.pop(0))
        return model.wrap(t, (hi << 64) | lo)
    if t == "f64":
        return ("bits64", int(toks.pop(0)))
    if t == "f32":
        return ("bits32", int(toks.pop(0)))
    raise AssertionError(t)


def expected_scalar(t, v):
    """The oracle value in the SAME encoding `parse_scalar` produces."""
    if t == "f64":
        return ("bits64", model.f64_bits(v))
    if t == "f32":
        return ("bits32", model.f32_bits(model.as_f32(v)))
    return v
