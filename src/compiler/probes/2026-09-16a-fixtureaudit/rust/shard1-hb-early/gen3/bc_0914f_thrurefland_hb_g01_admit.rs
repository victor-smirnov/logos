// TWIN of tests/logos/pass/bc_0914f_thrurefland_hb_g01_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14f-thrurefland, program g01 — caught: refuted an arm, verdict moved base -> landed, predicted wrong — PROBES.md "Build 2 left g01 refused: the generic base struct is absent after mono" | "Wrong in the predictions: g01 needed a third build (monomorph...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/land14f_d2.978t/g01_generic_struct_ref_arg_legal.logos
struct P { a: i64, b: i64 }
struct W<T> { v: T, k: i64 }
fn __logos_main() -> i32 {
    let s1: P = P { a: 1i64, b: 2i64 };
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut w: W<&P> = W { v: &s1, k: 0i64 };
    let a: &i64 = &w.v.a;
    w = W { v: &s2, k: 1i64 };
    return (*a + w.v.b) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

