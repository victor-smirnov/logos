// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h31_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h31 — caught: refuted fnptrelide/fnptrbinder: REFUSED this legal program — two unannotated closure parameters
// legality: by reading, no rustc binary
fn __logos_main() -> i32 {
    let f: fn(&i64, &i64) -> i64 = |a, b| { return *a + *b; };
    let x: i64 = 30i64;
    let y: i64 = 1i64;
    return f(&x, &y) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

