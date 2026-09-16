// TWIN of tests/logos/pass/bc_0912d_slicehop_hb_l04_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-12d-slicehop, program L04 — caught: refuted an arm — PROBES.md "still refuses L04 and L15 ... the same escape deposit that refuses L04 and L15"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/R0912d/ce/L04_slice_elem_from_slice_elem.logos
fn f<'a>(x: &'a i64, w: &'a i64) -> &'a i64 {
  let mut out: [&i64; 2] = [x, w];
  {
    let s: &mut [&i64] = &mut out;
    s[0u64] = s[1u64];
  }
  return out[0u64];
}
fn __logos_main() -> i32 { return 0i32; }

fn main() { std::process::exit(__logos_main() as i32); }

