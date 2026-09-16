// TWIN of tests/logos/pass/bc_0914b_ptrcoerceland_hb_m11_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14b-ptrcoerceland, program M11 — caught: refuted an arm — PROBES.md "build 3 ... M5 M10 M11 E02 E09"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land14a_twins4.YY4f/M11_arg_elided_region_param_mutptr_from_mutref_static.logos
static G: i64 = 7i64;
fn raw(q: *mut &i64) -> i64 {
    return unsafe { **q };
}
fn __logos_main() -> i32 {
    let mut r: &'static i64 = &G;
    return raw(&mut r) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

