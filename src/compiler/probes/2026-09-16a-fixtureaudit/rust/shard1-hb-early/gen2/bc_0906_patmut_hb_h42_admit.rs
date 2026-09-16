// TWIN of tests/logos/pass/bc_0906_patmut_hb_h42_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06-patmut, program h42 — caught: verdict moved base -> landed — PROBES.md "LEGAL, base REFUSED -> whole RUNS rc 0 (18)" + PROBES.md "The pricing's 42 hand shapes re-run on both builds: identical"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06-patmut/hand/h42_for_arr_i64_mut_reassign.logos
fn __logos_main() -> i32 {
    let arr: [i64; 2] = [1i64, 2i64];
    let mut acc: i64 = 0i64;
    for mut n in arr {
        n = n + 10i64;
        acc = acc + n;
    }
    if acc != 23i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

