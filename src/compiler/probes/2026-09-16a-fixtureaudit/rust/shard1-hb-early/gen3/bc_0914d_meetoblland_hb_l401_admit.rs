// TWIN of tests/logos/pass/bc_0914d_meetoblland_hb_l401_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14d-meetoblland, program L401 — caught: verdict moved base -> landed — PROBES.md "L401 X410 LEGAL refused / ILLEGAL admitted on base: no raw-pointer arm ... -> moblptr" (landed)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/invnb.tuCq/L401_struct_inv_binder_bound.logos
struct C<'a> { m: *mut &'a i64, r: &'a i64 }
fn mk<'a, 'b: 'a>(m: *mut &'a i64, r: &'b i64) -> C<'a> {
    return C { m: m, r: r };
}
fn __logos_main() -> i32 {
    let c: i64 = 5i64;
    return 5i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

