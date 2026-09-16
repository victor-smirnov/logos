// TWIN of tests/logos/pass/bc_0914j_fnptrbinder_hb_h25_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14j-fnptrbinder, program h25 — caught: refuted fnptrelide/fnptrbinder: REFUSED this legal program — the join carries 'a, which the enclosing fn also declares
// legality: by reading, no rustc binary
fn ida<'a>(x: &'a i64) -> i64 { return *x; }
fn idb<'a>(x: &'a i64) -> i64 { return *x + 2i64; }
fn test<'a>(q: &'a i64) -> i32 {
    let c: bool = false;
    let f: fn(&i64) -> i64 = if c { ida } else { idb };
    return f(q) as i32;
}
fn __logos_main() -> i32 {
    let v: i64 = 23i64;
    return test(&v);
}

fn main() { std::process::exit(__logos_main() as i32); }

