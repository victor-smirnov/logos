// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h06_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h06 — caught: refuted fnptrelide/fnptrbinder: REFUSED this legal program — one item binder offered two placeholders, first wins
// legality: by reading, no rustc binary
fn add2<'a>(a: &'a i64, b: &'a i64) -> i64 { return *a + *b; }
fn __logos_main() -> i32 {
    let f: fn(&i64, &i64) -> i64 = add2;
    let x: i64 = 10i64;
    let y: i64 = 6i64;
    return f(&x, &y) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

