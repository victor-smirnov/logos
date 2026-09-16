// TWIN of tests/logos/pass/bc_0914e_thruref_hb_m2_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program m2 — caught: verdict moved base -> landed — PROBES.md "asgref (7 moved, all LEGAL, all RUN): m1 m2 m5 t1 t2 t3"; landed 2026-09-14f, measured base REFUSED -> today RUN
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/letk.7cSu/m2_let_mutfield_reassign_refvar_legal.logos
struct P { a: i64, b: i64 }
fn __logos_main() -> i32 {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let mut s2: P = P { a: 3i64, b: 4i64 };
    let mut r: &mut P = &mut s1;
    let a: &mut i64 = &mut r.a;
    r = &mut s2;
    *a = 7i64;
    r.a = 5i64;
    return (s1.a - 7i64) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

