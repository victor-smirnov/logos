// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h16_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h16 — caught: refuted fnptrelide/fnptrbinder: REFUSED this legal program — an if-join of two fn items is a fn pointer carrying one item's named region
// legality: by reading, no rustc binary
fn ida<'a>(x: &'a i64) -> i64 { return *x; }
fn idb<'b>(x: &'b i64) -> i64 { return *x + 2i64; }
fn __logos_main() -> i32 {
    let c: bool = true;
    let f: fn(&i64) -> i64 = if c { ida } else { idb };
    let v: i64 = 24i64;
    return f(&v) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

