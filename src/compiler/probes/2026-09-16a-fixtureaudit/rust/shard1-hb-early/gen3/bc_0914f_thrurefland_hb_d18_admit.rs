// TWIN of tests/logos/pass/bc_0914f_thrurefland_hb_d18_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14f-thrurefland, program d18 — caught: verdict moved base -> landed — PROBES.md "tuple / struct LOCAL holding a reference (`&t.0.a`, `&h.r.b`, nested `h.m.p.a`) CLOSED, same reader, the path-type walk (d02 d03 d16 d18...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/land14f_d2.978t/d18_struct_mutref_field_legal.logos
struct P { a: i64, b: i64 }
struct H<'x> { r: &'x mut P }
fn __logos_main() -> i32 {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let mut s2: P = P { a: 3i64, b: 4i64 };
    let mut h: H = H { r: &mut s1 };
    let a: &mut i64 = &mut h.r.a;
    h = H { r: &mut s2 };
    *a = 4i64;
    h.r.b = 1i64;
    return (s1.a + s2.b) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

