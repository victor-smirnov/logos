// TWIN of tests/logos/pass/bc_0914k_fnptrbinderland_hb_c02_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14k-fnptrbinderland, program c02 — caught: legal, refused with the item-binder rename control-reverted (fnptrnoitembind): a method path carries the method's own region
// legality: by reading, no rustc binary
struct S { n: i64 }
impl S {
    fn pickn<'a>(a: &'a i64, b: &'a i64) -> i64 { return *a + *b; }
}
fn __logos_main() -> i32 {
    let f: fn(&i64, &i64) -> i64 = S::pickn;
    let x: i64 = 40i64;
    let y: i64 = 2i64;
    return f(&x, &y) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

