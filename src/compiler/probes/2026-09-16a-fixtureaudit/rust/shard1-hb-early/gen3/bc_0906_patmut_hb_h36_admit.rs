// TWIN of tests/logos/pass/bc_0906_patmut_hb_h36_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06-patmut, program h36 — caught: verdict moved base -> landed — PROBES.md "LEGAL, base REFUSED -> whole RUNS rc 0 (18)" + PROBES.md "The pricing's 42 hand shapes re-run on both builds: identical"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06-patmut/hand/h36_for_tuple_pat_mut_reassign.logos
fn __logos_main() -> i32 {
    let arr: [(i64, i64); 2] = [(1i64, 2i64), (3i64, 4i64)];
    let mut acc: i64 = 0i64;
    for (mut a, b) in arr {
        a = a + b;
        acc = acc + a;
    }
    if acc != 10i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

