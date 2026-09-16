// TWIN of tests/logos/pass/bc_0909c_atbind_hb_h14_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09c-atbind, program h14 — caught: refuted an arm — PROBES.md "atletdel ... ⚠ hand h14 (`let mut n @ _`) still refused ... legal Rust"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-09c-atbind/hand/h14.logos
// h14 LEGAL: `let mut n @ _`, then assigned. Rust: 0
fn __logos_main() -> i32 {
    let mut n @ _ = 1i64;
    n = 2i64;
    if n != 2i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

