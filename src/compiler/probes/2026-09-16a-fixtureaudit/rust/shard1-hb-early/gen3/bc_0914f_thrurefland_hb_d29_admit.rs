// TWIN of tests/logos/pass/bc_0914f_thrurefland_hb_d29_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14f-thrurefland, program d29 — caught: verdict moved base -> landed — PROBES.md "ref-typed LOCAL root (`&r.a`, `r = &s2`), let / match-default / written walk CLOSED (queue row; d01 d06 d09 d12 d24 d29 d30 d31, t1-t3)"...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/land14f_d2.978t/d29_loop_alternating_targets_legal.logos
struct P { a: i64, b: i64 }
fn __logos_main() -> i32 {
    let s1: P = P { a: 1i64, b: 2i64 };
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut r: &P = &s1;
    let mut i: i64 = 0i64;
    let mut sum: i64 = 0i64;
    while i < 4i64 {
        let a: &i64 = &r.b;
        if i % 2i64 == 0i64 { r = &s2; } else { r = &s1; }
        sum = sum + *a;
        i = i + 1i64;
    }
    return sum as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

