// TWIN of tests/logos/pass/bc_0914k_fnptrbinderland_hb_c12_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14k-fnptrbinderland, program c12 — caught: legal, refused on base (item 'a in a W<'a> return vs fn(&i64) -> W<'_>), admitted by the landing
// legality: by reading, no rustc binary
struct W<'a> { r: &'a i64 }
fn mkw<'a>(x: &'a i64) -> W<'a> { return W { r: x }; }
fn __logos_main() -> i32 {
    let f: fn(&i64) -> W<'_> = mkw;
    let v: i64 = 12i64;
    let w: W = f(&v);
    return *w.r as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

