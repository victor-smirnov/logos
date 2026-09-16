// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h15_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h15 — caught: refuted fnptrelidesup: REFUSED this legal program (the let of a named fn item before the reassignment)
// legality: by reading, no rustc binary
fn ida<'a>(x: &'a i64) -> i64 { return *x; }
fn idb(x: &i64) -> i64 { return *x + 1i64; }
fn __logos_main() -> i32 {
    let mut f: fn(&i64) -> i64 = ida;
    f = idb;
    let v: i64 = 24i64;
    return f(&v) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

