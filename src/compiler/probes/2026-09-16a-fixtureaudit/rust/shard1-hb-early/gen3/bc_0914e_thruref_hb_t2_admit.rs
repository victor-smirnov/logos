// TWIN of tests/logos/pass/bc_0914e_thruref_hb_t2_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program t2 — caught: verdict moved base -> landed — PROBES.md "asgref (7 moved, all LEGAL, all RUN): m1 m2 m5 t1 t2 t3"; landed 2026-09-14f, measured base REFUSED -> today RUN
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/walk.ek13/t2_struct_door_reassign_refvar.logos
struct P { a: i64, b: i64 }
fn __logos_main() -> i32 {
    let s1: P = P { a: 1i64, b: 2i64 };
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut r: &P = &s1;
    let mut t: i64 = 0i64;
    match r {
        P { a, b } => {
            r = &s2;
            t = *a + r.a;
        }
    }
    return (t - 4i64) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

