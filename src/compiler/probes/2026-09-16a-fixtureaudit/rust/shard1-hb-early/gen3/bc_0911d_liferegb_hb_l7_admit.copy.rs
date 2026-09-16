// TWIN of tests/logos/pass/bc_0911d_liferegb_hb_l7_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: u64 index literal -> usize (Rust indices are usize)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-11d-liferegb, program L7 — caught: refuted an arm — PROBES.md "Legal and REFUSED (3) — none of them in any corpus column: L1 ... L7 ... L5b"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/lb/hand/L7.logos
fn f<'a>(x: &'a i64, y: &i64) -> &'a i64 {
  let mut arr: [&i64; 2] = [x, x];
  arr[1usize] = y;
  return arr[0usize];
}
fn __logos_main() -> i32 { return 0i32; }

fn main() { std::process::exit(__logos_main() as i32); }

