// TWIN of tests/logos/pass/bc_0909a_bindmut_hb_f14_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09a-bindmut, program f14 — caught: refuted an arm — PROBES.md "`'l: for mut i in 0..3` ABORTED the compiler — LIR-MIRROR-005 ... f06/f14 134 -> 0" (the priced scaffold)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-09a-bindmut/hand/f14_labeled_min_mut.logos
fn __logos_main() -> i32 {
    'l: for mut i in 0i64..3i64 {
        i = i + 1i64;
        if i == 2i64 { break 'l; }
    }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

