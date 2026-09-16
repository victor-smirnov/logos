// TWIN of tests/logos/pass/bc_0914k_fnptrbinderland_hb_c08_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14k-fnptrbinderland, program c08 — caught: legal, refused with the for<> binder rename control-reverted (fnptrnohrtb)
// legality: by reading, no rustc binary
fn id(x: &i64) -> &i64 { return x; }
fn __logos_main() -> i32 {
    let g: fn(&i64) -> &i64 = id;
    let f: for<'a> fn(&'a i64) -> &'a i64 = g;
    let h: fn(&i64) -> &i64 = f;
    let v: i64 = 8i64;
    return *h(&v) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

