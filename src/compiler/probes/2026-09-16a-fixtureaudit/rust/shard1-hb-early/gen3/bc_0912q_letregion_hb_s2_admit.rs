// TWIN of tests/logos/pass/bc_0912q_letregion_hb_s2_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-12q-letregion, program s2 — caught: refuted an arm — PROBES.md "DO NOT FUND door L as priced (`letnamed` / `letnamedc`). FOUR legal refusals in four shapes — p24 ..."
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/r0912q/ctl2/s2_legal_slice_index_param.logos
fn foo<'a>(xs: &'a [i64]) -> i64 {
    let r: &'a i64 = &xs[1];
    return *r;
}
fn __logos_main() -> i32 {
    let a: [i64; 3] = [1i64, 22i64, 3i64];
    return foo(&a) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

