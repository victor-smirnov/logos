// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h24_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h24 — caught: refuted fnptrelide/fnptrbinder: REFUSED this legal program — an unannotated closure parameter hinted from the pointer type
// legality: by reading, no rustc binary
fn __logos_main() -> i32 {
    let f: fn(&i64) -> i64 = |x| { return *x + 1i64; };
    let v: i64 = 33i64;
    return f(&v) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

