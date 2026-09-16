// TWIN of tests/logos/pass/bc_0912r_escroot_hb_y2_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-12r-escroot, program y2 — caught: verdict moved base -> landed — PROBES.md "Newly COMPILING, all legal, all run to their exit code: y1 y2 y3 (deref stop) · a6 a10 e3 m1 m3 m4 m5 o3"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/r0912r/ctl/y2_legal_struct_ref_local_field_escape.logos
struct W { f: i64 }
fn foo<'a>(w: &'a W, out: &mut &'a i64) {
    let w2: &'a W = w;
    *out = &w2.f;
}
fn __logos_main() -> i32 {
    let w: W = W { f: 13i64 };
    let z: i64 = 0i64;
    let mut r: &i64 = &z;
    foo(&w, &mut r);
    return *r as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

