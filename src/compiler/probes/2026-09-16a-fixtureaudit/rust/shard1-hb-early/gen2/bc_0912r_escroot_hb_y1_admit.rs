// TWIN of tests/logos/pass/bc_0912r_escroot_hb_y1_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-12r-escroot, program y1 — caught: verdict moved base -> landed — PROBES.md "Newly COMPILING, all legal, all run to their exit code: y1 y2 y3 (deref stop) · a6 a10 e3 m1 m3 m4 m5 o3"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/r0912r/ctl/y1_legal_slice_local_index_escape.logos
fn foo<'a>(xs: &'a [i64], out: &mut &'a i64) {
    let ys: &'a [i64] = xs;
    *out = &ys[1];
}
fn __logos_main() -> i32 {
    let arr: [i64; 3] = [4i64, 7i64, 9i64];
    let z: i64 = 0i64;
    let mut r: &i64 = &z;
    foo(&arr, &mut r);
    return *r as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

