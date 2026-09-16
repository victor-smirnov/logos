// TWIN of tests/logos/pass/bc_0912r_escroot_hb_k18_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-12r-escroot, program k18 — caught: refuted an arm — PROBES.md "escnd refuses LEGAL k17 k18"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/r0912r/ctl/k18_legal_escape_call_field_through_ref_local.logos
struct W { f: i64 }
fn id<'q>(x: &'q i64) -> &'q i64 {
    return x;
}
fn foo<'a>(w: &'a W, out: &mut &'a i64) {
    let w2: &'a W = w;
    *out = id(&w2.f);
}
fn __logos_main() -> i32 {
    let w: W = W { f: 42i64 };
    let z: i64 = 0i64;
    let mut r: &i64 = &z;
    foo(&w, &mut r);
    return *r as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

