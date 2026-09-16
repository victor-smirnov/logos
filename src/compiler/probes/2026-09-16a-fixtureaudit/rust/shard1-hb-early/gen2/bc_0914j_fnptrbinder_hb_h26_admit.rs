// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h26_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h26 — caught: refuted fnptrelide/fnptrbinder: REFUSED this legal program — h01's closure inside a fn with its own 'a
// legality: by reading, no rustc binary
fn t<'a>(q: &'a i64) -> i64 {
    let f: fn(&i64) -> i64 = |x: &i64| -> i64 { return *x + 1i64; };
    return f(q);
}
fn __logos_main() -> i32 {
    let v: i64 = 25i64;
    return t(&v) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

