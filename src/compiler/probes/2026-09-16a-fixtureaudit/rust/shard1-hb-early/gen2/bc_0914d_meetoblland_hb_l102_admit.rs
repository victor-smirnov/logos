// TWIN of tests/logos/pass/bc_0914d_meetoblland_hb_l102_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14d-meetoblland, program L102 — caught: predicted wrong — PROBES.md "PREDICTION MISSES: L102 predicted refused by default, compiled ...; X115 predicted refused, still admitted"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/landbat.gPIU/L102_struct_over_inv_enum_bound.logos
enum E<'a> { N, M(*mut &'a i64, &'a i64) }
struct Q<'a> { e: E<'a>, k: i64 }
fn mk<'a, 'b: 'a>(p: *mut &'a i64, r: &'b i64) -> Q<'a> {
    let e = E::M(p, r);
    return Q { e: e, k: 4i64 };
}
fn __logos_main() -> i32 {
    let q: Q = Q { e: E::N, k: 4i64 };
    return q.k as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

