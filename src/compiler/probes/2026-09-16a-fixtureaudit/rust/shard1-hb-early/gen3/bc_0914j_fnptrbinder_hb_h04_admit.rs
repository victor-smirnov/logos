// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h04_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h04 — caught: refuted fnptrelidesup: REFUSED this legal program at a return site
// legality: by reading, no rustc binary
fn id<'x>(x: &'x i64) -> &'x i64 { return x; }
fn make() -> fn(&i64) -> &i64 {
    return id;
}
fn __logos_main() -> i32 {
    let g = make();
    let v: i64 = 14i64;
    return *g(&v) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

