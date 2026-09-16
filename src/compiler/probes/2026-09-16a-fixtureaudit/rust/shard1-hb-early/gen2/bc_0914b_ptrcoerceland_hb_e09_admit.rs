// TWIN of tests/logos/pass/bc_0914b_ptrcoerceland_hb_e09_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14b-ptrcoerceland, program E09 — caught: refuted an arm — PROBES.md "build 3 ... M5 M10 M11 E02 E09"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land14a_twins5.F7Ma/E09_outlives_where_ref_to_constptr_arg.logos
fn take<'b>(p: *const &'b i64) -> i64 {
    return unsafe { **p };
}
fn pass<'a, 'b>(x: &'b &'a i64) -> i64 where 'a: 'b {
    return take(x);
}
fn __logos_main() -> i32 {
    let v: i64 = 8i64;
    let r: &i64 = &v;
    return pass(&r) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

