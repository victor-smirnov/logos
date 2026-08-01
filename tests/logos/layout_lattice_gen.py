#!/usr/bin/env python3
"""Generate the layout gate's two programs from the AXES, not from a list.

  lattice  — every shape must REACH `verify_layout_engines`, so each type is
             constructed and read back. This is what makes "0 disagreements" a
             statement about the shapes rather than about the stdlib.
  oracle   — every shape's layout is MEASURED at run time and asserted against
             what the compiler claims. Two facts are measured:
               * where a custom DST's `[T]` tail actually lands — by writing
                 through the fat pointer and scanning the allocation for the
                 byte that changed — against `sizeof::<Seg>()`;
               * where a field actually is — by a POINTER DIFFERENCE — against
                 `offset_of!`.
             Neither measurement asks a layout engine.

The composition axes are the ones whose branches were MISSING from an engine:
union (sum vs max), tagged enum, C-like enum, niche-packed enum, zero-sized
payload, generic instantiation, and nestings of those. A scalars-only lattice is
how three engines could lack a whole `is_union()` branch behind a green gate.
"""
import json
import sys

# 20 distinct scalar leaves — every integer width the language has, both
# signednesses, the odd widths included, plus the floats, bool and char.
SCALARS = ["i8", "u8", "i16", "u16", "i24", "u24", "i32", "u32", "i56", "u56",
           "i64", "u64", "i128", "u128", "isize", "usize", "f32", "f64",
           "bool", "char"]
SHAPES = ["iso", "adj", "nnw", "tail", "nest", "narrow"]

# EVERY BACKING WIDTH A C-LIKE ENUM CAN DECLARE. This axis did not exist, and
# that is the whole reason a compiler shipped with `enum B : u64` weighing four
# bytes to mono: the only C-like enum in the lattice was `enum EC { X, Y, Z }`,
# whose backing type is the DEFAULT i32 — the one width at which a missing
# backing branch and a correct one give the same answer. One value of an axis is
# not an axis.
BACKINGS = ["u8", "i8", "u16", "i16", "u24", "i24",
            "u32", "i32", "u56", "i56", "u64", "i64"]

# Composition axes. Each entry is (tag, type-expression, initialiser-expression).
# The initialiser is what makes the type REACHABLE, which is what makes it
# REGISTERED, which is what makes the verifier see it.
def composition_axes():
    axes = []
    # Tagged enum over every payload width that changes the answer: the payload
    # alignment decides where the payload starts and whether the disc word gets
    # its own padded slot.
    for t in ["i8", "u8", "i16", "u24", "i32", "i56", "i64", "i128", "f32",
              "f64", "bool", "char"]:
        axes.append((f"opt_{t}", f"Option<{t}>", f"Option::Some({lit(t)})"))
    # ZERO-SIZED payload — a payload of no bytes is no bytes, not one byte.
    axes.append(("opt_zst", "Option<Zst>", "Option::Some(Zst {})"))
    axes.append(("opt_none_i64", "Option<i64>", "Option::None"))
    # NICHE-PACKED — the disc lives in the pointer's null value, so the enum is
    # exactly its payload and there is no disc word at all.
    axes.append(("opt_ref", "Option<&i64>", "Option::Some(&NINE)"))
    # NESTED enum — the inner enum's full inline footprint, not a pointer.
    axes.append(("opt_opt", "Option<Option<i64>>", "Option::Some(Option::Some(7i64))"))
    # Authored enums: two payload arms of different widths, and a C-like enum
    # whose size is its discriminant.
    axes.append(("e_two", "E2", "E2::B(5i64)"))
    axes.append(("e_clike", "EC", "EC::Y"))
    # A NON-GENERIC niche-packed enum. `Option<&i64>` covers the niche RULE, but
    # it is generic, and sema names a generic enum before mono renames it — so
    # sema's niche answers never matched a key the verifier had. Measured: the
    # `sema_abi_layout × niche` cell of the ENGINE × SHAPE matrix was at ZERO.
    axes.append(("e_niche", "ORef", "ORef::S(&NINE)"))
    # C-LIKE AT EVERY BACKING WIDTH — the axis whose absence hid the bug.
    for w in BACKINGS:
        axes.append((f"eb_{w}", f"EB_{w}", f"EB_{w}::Y"))
    # …and GENERIC at every backing width: an instance has to CARRY its backing
    # type through monomorphisation. `clone_enum_def` used to drop it, so
    # `GB_u64<i32>` weighed four bytes where `EB_u64` weighed eight — the same
    # wrong number, one phase further down, reached by a different route.
    for w in BACKINGS:
        # NOTE the spelling: `GB_u8::Y`, not `GB_u8::<i32>::Y`. The turbofish
        # form of a payload-LESS variant of a generic enum is lowered as an
        # enum literal WITH data and mlir-gen rejects it ("unknown tagged enum
        # 'GB_u8__i32'"), then DROPS the statement and still exits 0. That is a
        # separate defect, in expression lowering rather than in layout; it is
        # written down here so the spelling is a known constraint and not a
        # coincidence somebody later "cleans up".
        axes.append((f"gb_{w}", f"GB_{w}<i32>", f"GB_{w}::Y"))
    # UNIONS — where the accumulation rule INVERTS (sum becomes max).
    axes.append(("u_big", "UBig", "UBig { big: 7i64 }"))
    axes.append(("u_narrow", "UNarrow", "UNarrow { x: 3i32 }"))
    axes.append(("u_align", "UAlign", "UAlign { w: 11i128 }"))
    # GENERIC instantiations — a generic has no layout until instantiated, and
    # the engines that read its fields UNSUBSTITUTED sized every one of them at
    # the {8,8} unknown default.
    for t in ["u8", "i32", "i64", "i128", "f32"]:
        axes.append((f"w_{t}", f"Wrap<{t}>", f"Wrap {{ v: {lit(t)}, n: 1u8 }}"))
    axes.append(("w_opt", "Wrap<Option<i32>>",
                 "Wrap { v: Option::Some(3i32), n: 1u8 }"))
    # NESTING of the composition shapes in each other.
    axes.append(("n_ou", "NestOU", "NestOU { o: Option::Some(3i32), u: UBig { big: 1i64 } }"))
    axes.append(("n_wo", "NestWO",
                 "NestWO { w: Wrap { v: 5i64, n: 2u8 }, o: Option::Some(9i8) }"))
    # Arrays and tuples of a composition shape.
    axes.append(("a_opt", "[Option<i32>; 3]",
                 "[Option::Some(1i32), Option::Some(2i32), Option::Some(3i32)]"))
    axes.append(("t_ou", "(Option<i32>, i64)", "(Option::Some(4i32), 8i64)"))
    return axes


def lit(t):
    if t == "bool":
        return "true"
    if t == "char":
        return "'a'"
    if t in ("f32", "f64"):
        return f"1.0{t}"
    return f"1{t}"


USES = "use logos.lang.option;"

PRELUDE = """struct Zst {}
enum E2 { A(i32), B(i64) }
enum EC { X, Y, Z }
enum ORef { N, S(&i64) }
""" + "".join(f"enum EB_{w} : {w} {{ X, Y, Z }}\n" for w in BACKINGS) \
    + "".join(f"enum GB_{w}<T> : {w} {{ X, Y, Z }}\n" for w in BACKINGS) + """
union UBig { bytes: [u8; 12], big: i64 }
union UNarrow { x: i32, y: u8 }
union UAlign { w: i128, b: [u8; 4] }
struct Wrap<T> { pub v: T, pub n: u8 }
struct NestOU { pub o: Option<i32>, pub u: UBig }
struct NestWO { pub w: Wrap<i64>, pub o: Option<i8> }
static NINE: i64 = 9i64;
"""


def gen_lattice(path):
    out = ["package layout_gate_lattice;", USES, PRELUDE]
    mains = []
    n = 0
    for t in SCALARS:
        for sh in SHAPES:
            sn = f"S_{t}_{sh}"
            if sh == "iso":
                fs = [("p", t), ("id", "i64")]
            elif sh == "adj":
                fs = [("p", t), ("n", "u8"), ("id", "i64")]
            elif sh == "nnw":
                fs = [("n", "u8"), ("p", t), ("m", "u24"), ("id", "i64")]
            elif sh == "tail":
                fs = [("id", "i64"), ("p", t), ("n", "u8")]
            elif sh == "narrow":
                fs = [("p", t), ("n", "u8"), ("m", "u24")]
            else:  # nest
                out.append(f"struct In_{t} {{ pub p: {t}, pub n: u8 }}")
                n += 1
                fs = None
            if fs is None:
                out.append(f"struct {sn} {{ pub inner: In_{t}, pub id: i64 }}")
                init = f"{sn} {{ inner: In_{t} {{ p: {lit(t)}, n: 1u8 }}, id: 7i64 }}"
            else:
                out.append("struct " + sn + " { "
                           + ", ".join(f"pub {nm}: {ty}" for nm, ty in fs) + " }")

                def v(nm):
                    if nm == "p":
                        return lit(t)
                    if nm == "id":
                        return "7i64"
                    if nm == "n":
                        return "1u8"
                    return "2u24"
                init = sn + " { " + ", ".join(f"{nm}: {v(nm)}" for nm, _ in fs) + " }"
            n += 1
            probe = "n" if sh == "narrow" else "id"
            acc = ".inner.n" if (sh == "nest" and probe == "n") else "." + probe
            mains.append(f"    {{ let a: [{sn}; 2] = [{init}, {init}]; "
                         f"if a[1i64]{acc} != a[0i64]{acc} {{ return 1; }} }}")

    # The composition block: each axis becomes a struct with the shape BEFORE a
    # wide field and a struct with it AFTER, so the padding it induces is
    # exercised on both sides.
    for tag, ty, init in composition_axes():
        for pos in ("pre", "post"):
            sn = f"C_{tag}_{pos}"
            if pos == "pre":
                out.append(f"struct {sn} {{ pub c: {ty}, pub id: i64 }}")
                mains.append(f"    {{ let x: {sn} = {sn} {{ c: {init}, id: 7i64 }}; "
                             f"if x.id != 7i64 {{ return 1; }} }}")
            else:
                out.append(f"struct {sn} {{ pub id: i64, pub c: {ty}, pub n: u8 }}")
                mains.append(f"    {{ let x: {sn} = {sn} {{ id: 7i64, c: {init}, n: 1u8 }}; "
                             f"if x.n != 1u8 {{ return 1; }} }}")
            n += 1

    out.append("")
    out.append("unsafe fn main() -> i64 {")
    out.extend(mains)
    out.append("    return 0;")
    out.append("}")
    open(path, "w").write("\n".join(out) + "\n")
    sys.stderr.write(f"[layout-gate] lattice generated: {n} types\n")
    # A STRUCTURED verdict: the gate reads this with tests/logos/verdict.py, not
    # with `sed`. `sed -nE 's/^LATTICE_TYPES=([0-9]+)$/\\1/p'` was safe by luck —
    # it printed nothing on a non-match — but the same file's `, N defs,` scrape
    # was not, and a gate should not have two idioms for reading one number.
    sys.stderr.write("lattice-gen-json: " + json.dumps({"types": n}) + "\n")


def gen_oracle(path, canary=False):
    """A program that MEASURES what it is told and returns non-zero on a lie.

    With `canary=True` the FIRST probe's comparison is INVERTED — the program
    then returns code 1 exactly when the compiler is right. The gate generates
    both and runs them through the same compile, the same link and the same
    `rc != 0` reading; the canary proves that probe is reachable, that its
    verdict becomes an exit code and that the exit code reaches the gate. A
    generator that emitted an oracle whose probes never ran would produce a
    canary that exits 0, and the gate reports itself broken.
    """
    prefixes = [("i64", "i64"), ("u8", "u8"), ("i128", "i128"),
                ("f64", "f64"), ("char", "char")]
    prefixes += [(tag, ty) for tag, ty, _ in composition_axes()
                 if not ty.startswith("[") and not ty.startswith("(")]

    out = ["package layout_gate_oracle;",
           USES,
           "extern fn malloc(size: i64) -> *mut u8;",
           PRELUDE]
    for tag, ty in prefixes:
        out.append(f"struct Seg_{tag} {{ h: {ty}, data: [u8] }}")
    out.append("")
    # One probe per prefix shape: zero a generous buffer, hand it to the fat
    # pointer, write ONE byte through the tail, then scan for it. The index the
    # scan finds is where the tail really begins — no engine was asked.
    for tag, ty in prefixes:
        out.append(f"""unsafe fn tail_at_{tag}(cap: i64) -> i64 {{
    let raw: *mut u8 = malloc(256 + cap);
    let mut i: i64 = 0;
    while i < 256 + cap {{ *(raw.add(i)) = 0u8; i = i + 1; }}
    let s: &mut Seg_{tag} = dst_from_raw_parts_mut::<Seg_{tag}>(raw, cap);
    s.data[0] = 171u8;
    i = 0;
    while i < 256 + cap {{ if *(raw.add(i)) == 171u8 {{ return i; }} i = i + 1; }}
    return -1;
}}""")

    # offset_of! against a pointer difference, for a family of sized structs
    # spanning the same composition axes.
    off_structs = []
    for tag, ty, init in composition_axes():
        if ty.startswith("[") or ty.startswith("("):
            continue
        off_structs.append((f"O_{tag}", ty, init))
    for sn, ty, init in off_structs:
        out.append(f"struct {sn} {{ pub a: u8, pub c: {ty}, pub z: i64 }}")

    # ── THE DstRef PREFIX PROBE — THE ONLY NET FOR MONO'S PROJECTION ────────
    # A generic `?Sized` method on a smart-pointer shape is lowered ONCE by sema
    # with the inner pointer THIN, and mono RE-LOWERS it per instantiation into a
    # byte-offset projection off the fat pointer's data half. That offset comes
    # from `mono_abi_layout`, and NOTHING ELSE reads it: it is not a struct size
    # any registry holds, so the compile-time verifier cannot see it. Only a
    # running program can.
    #
    # The oracle is a comparison between TWO WRITES TO THE SAME FIELD, one
    # through the sized instantiation and one through the `dyn` one, each
    # located by scanning its own zeroed buffer for the byte that changed. No
    # engine is consulted for either number. `offset_of!` is then checked as a
    # third, CLAIMED, value — so a compiler that got both projections equally
    # wrong is still caught.
    out.append("""
trait QTr { fn v(&self) -> i64; }
struct QA { x: i64 }
impl QTr for QA { fn v(&self) -> i64 { return self.x; } }
unsafe fn qzero(n: i64) -> *mut u8 {
    let raw: *mut u8 = malloc(n);
    let mut i: i64 = 0;
    while i < n { *(raw.add(i)) = 0u8; i = i + 1; }
    return raw;
}
unsafe fn qscan(raw: *mut u8, n: i64) -> i64 {
    let mut i: i64 = 0;
    while i < n { if *(raw.add(i)) == 171u8 { return i; } i = i + 1; }
    return -1;
}""")
    for w in BACKINGS:
        out.append(f"""struct QI_{w}<T: ?Sized> {{ h: EB_{w}, k: u8, val: T }}
struct QR_{w}<T: ?Sized> {{ inner: *mut QI_{w}<T> }}
impl<T: ?Sized> QR_{w}<T> {{
    fn setk(&mut self, v: u8) {{ unsafe {{ self.inner.k = v; }} }}
}}
// Through the SIZED instantiation.
unsafe fn qk_sized_{w}() -> i64 {{
    let raw: *mut u8 = qzero(128i64);
    let p: *mut QI_{w}<QA> = raw as *mut QI_{w}<QA>;
    let mut rs: QR_{w}<QA> = QR_{w}::<QA> {{ inner: p }};
    rs.setk(171u8);
    return qscan(raw, 128i64);
}}
// Through the `dyn` instantiation — mono's DstRef re-lowering. Same field, same
// declaration, a DIFFERENT code path for its offset.
unsafe fn qk_dyn_{w}() -> i64 {{
    let raw: *mut u8 = qzero(128i64);
    let p: *mut QI_{w}<QA> = raw as *mut QI_{w}<QA>;
    let rs: QR_{w}<QA> = QR_{w}::<QA> {{ inner: p }};
    let mut rd: QR_{w}<dyn QTr> = rs as QR_{w}<dyn QTr>;
    rd.setk(171u8);
    return qscan(raw, 128i64);
}}
// A GENERIC C-like enum's backing width has to survive monomorphisation.
struct GH_{w}<T> {{ pub h: GB_{w}<T>, pub k: u8, pub v: i64 }}
unsafe fn gk_{w}() -> i64 {{
    let raw: *mut u8 = qzero(128i64);
    let p: *mut GH_{w}<i32> = raw as *mut GH_{w}<i32>;
    p.k = 171u8;
    return qscan(raw, 128i64);
}}""")

    out.append("")
    out.append("unsafe fn main() -> i32 {")
    code = 1
    for tag, ty in prefixes:
        cmp_op = "==" if (canary and code == 1) else "!="
        out.append(f"""    {{
        let claimed: i64 = sizeof::<Seg_{tag}>() as i64;
        let measured: i64 = tail_at_{tag}(8i64);
        // The tail must begin exactly at the end of the sized prefix: a LOWER
        // offset overlaps the last prefix field, a HIGHER one writes past the
        // `sizeof + cap` bytes a caller allocates.
        if measured {cmp_op} claimed {{ return {code}i32; }}
    }}""")
        code += 1
    for sn, ty, init in off_structs:
        out.append(f"""    {{
        let x: {sn} = {sn} {{ a: 1u8, c: {init}, z: 5i64 }};
        let base: i64 = ((&x) as *const {sn} as *const u8) as i64;
        if offset_of!({sn}, c) as i64 != ((&x.c) as *const {ty} as *const u8) as i64 - base {{
            return {code}i32;
        }}
        if offset_of!({sn}, z) as i64 != ((&x.z) as *const i64 as *const u8) as i64 - base {{
            return {code + 1}i32;
        }}
        if x.a != 1u8 || x.z != 5i64 {{ return {code + 2}i32; }}
    }}""")
        code += 3
    # A union's fields are ALL at offset zero — the one place the accumulator
    # must not be used, and the place sema used it.
    out.append("""    {
        let u: UBig = UBig { big: 7i64 };
        let base: i64 = ((&u) as *const UBig as *const u8) as i64;
        if offset_of!(UBig, big) as i64 != 0i64 { return %di32; }
        if offset_of!(UBig, bytes) as i64 != 0i64 { return %di32; }
        if ((&u.big) as *const i64 as *const u8) as i64 - base != 0i64 { return %di32; }
    }""" % (code, code + 1, code + 2))
    code += 3
    # THE DstRef PREFIX ROWS. Three facts per backing width, each a distinct
    # code so the failure names the width and which of the three broke.
    for w in BACKINGS:
        out.append(f"""    {{
        let s: i64 = qk_sized_{w}();
        let d: i64 = qk_dyn_{w}();
        // (1) TWO MEASUREMENTS, NO ENGINE. The same field written through the
        //     sized and the `dyn` instantiation must land on the same byte.
        if s != d {{ return {code}i32; }}
        // (2) …and where the compiler CLAIMS it is.
        if d != offset_of!(QI_{w}<QA>, k) as i64 {{ return {code + 1}i32; }}
        // (3) a generic C-like enum weighs what its backing type weighs, and
        //     the field after it lands accordingly.
        if gk_{w}() != offset_of!(GH_{w}<i32>, k) as i64 {{ return {code + 2}i32; }}
        if sizeof::<GB_{w}<i32>>() as i64 != sizeof::<EB_{w}>() as i64 {{
            return {code + 3}i32;
        }}
    }}""")
        code += 4
    out.append("    return 0i32;")
    out.append("}")
    open(path, "w").write("\n".join(out) + "\n")
    what = "run oracle CANARY" if canary else "run oracle"
    sys.stderr.write(f"[layout-gate] {what} generated: {len(prefixes)} DST prefix "
                     f"shapes, {len(off_structs)} offset_of shapes, "
                     f"{len(BACKINGS)} DstRef-projection widths, "
                     f"{code - 1} distinct failure codes\n")
    # The counts are the gate's floors, read back from the loop that emitted
    # them rather than maintained by hand.
    sys.stderr.write("oracle-gen-json: " + json.dumps({
        "prefixes": len(prefixes), "offsets": len(off_structs),
        "dstref_widths": len(BACKINGS), "codes": code - 1,
        "canary": 1 if canary else 0}) + "\n")


if __name__ == "__main__":
    mode, path = sys.argv[1], sys.argv[2]
    if mode == "lattice":
        gen_lattice(path)
    elif mode == "oracle":
        gen_oracle(path)
    elif mode == "oracle-canary":
        gen_oracle(path, canary=True)
    else:
        sys.exit(f"unknown mode {mode}")
