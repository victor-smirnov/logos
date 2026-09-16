// TWIN of tests/logos/pass/bc_0906_patmut_hb_h19_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.collections.vec;` dropped (prelude in Rust)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06-patmut, program h19 — caught: verdict moved base -> landed — PROBES.md "LEGAL, base REFUSED -> whole RUNS rc 0 (18)" + PROBES.md "The pricing's 42 hand shapes re-run on both builds: identical"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06-patmut/hand/h19_for_mut_reassign.logos
fn __logos_main() -> i32 {
    let mut vs: Vec<i64> = Vec::new();
    vs.push(1i64);
    let mut acc: i64 = 0i64;
    for mut n in vs {
        n = n + 7i64;
        acc = acc + n;
    }
    if acc != 8i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

