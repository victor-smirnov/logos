// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h02_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h02 — caught: refuted the rule-9 twin fnptrelidesup: REFUSED this legal program — a fn item's own 'a against an elided fn-pointer slot
// legality: by reading, no rustc binary
fn idr<'a>(x: &'a i64) -> i64 { return *x; }
fn __logos_main() -> i32 {
    let f: fn(&i64) -> i64 = idr;
    let v: i64 = 12i64;
    return f(&v) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

