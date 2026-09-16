// TWIN of tests/logos/pass/bc_0914f_thrurefland_hb_d22_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14f-thrurefland, program d22 — caught: verdict moved base -> landed — PROBES.md "tuple / struct LOCAL holding a reference (`&t.0.a`, `&h.r.b`, nested `h.m.p.a`) CLOSED, same reader, the path-type walk (d02 d03 d16 d18...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/land14f_d2.978t/d22_nested_struct_ref_two_levels_legal.logos
struct P { a: i64, b: i64 }
struct M<'x> { p: &'x P }
struct H<'y> { m: M<'y>, k: i64 }
fn __logos_main() -> i32 {
    let s1: P = P { a: 1i64, b: 2i64 };
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut h: H = H { m: M { p: &s1 }, k: 0i64 };
    let a: &i64 = &h.m.p.a;
    h = H { m: M { p: &s2 }, k: 1i64 };
    return (*a + h.m.p.b) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

