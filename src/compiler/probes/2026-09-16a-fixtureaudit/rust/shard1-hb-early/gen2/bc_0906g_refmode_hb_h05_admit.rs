// TWIN of tests/logos/pass/bc_0906g_refmode_hb_h05_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06g-refmode, program h05 — caught: refuted an arm — PROBES.md "Under `tupboth` ... FOUR do not — h01 h05 h19 h20 — ... use of moved field 't.0'"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06g-refmode/hand/h05.logos
// LEGAL. `ref mut` in a tuple, passed to a fn taking &mut i64.
fn bump(r: &mut i64) { *r = *r + 100i64; }
fn __logos_main() -> i32 {
    let mut t: (i64, i64) = (3i64, 4i64);
    match t {
        (ref mut a, b) => { bump(a); if *a + b != 107i64 { return 1i32; } }
    }
    if t.0 != 103i64 { return 2i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

