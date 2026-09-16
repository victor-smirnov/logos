// TWIN of tests/logos/pass/bc_0909d_atland_hb_g14_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.string;` dropped (prelude in Rust)
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09d-atland, program g14 — caught: refuted an arm, verdict moved base -> landed — PROBES.md "(b) g14 ABORTED (134) under the first `let` delegation" | (b) tables/hand_head.txt vs hand_final.txt: run=1 or cc=1 on HEAD -> cc=0 run=...
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-09d-atland/hand/g14.logos
// A `let` at-binding over a String, then the binding is USED and DROPPED once.
fn __logos_main() -> i32 {
    let s: String = String::from("hello");
    let w @ _ = s;
    if w.len() as i64 != 5i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

