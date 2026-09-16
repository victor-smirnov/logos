// TWIN of tests/logos/pass/bc_0911d_liferegb_hb_l1_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-11d-liferegb, program L1 — caught: refuted an arm — PROBES.md "Legal and REFUSED (3) — none of them in any corpus column: L1 ... L7 ... L5b"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/lb/hand/L1.logos
struct P { a: &i64, b: &i64 }
fn f<'a>(x: &'a i64, y: &i64) -> &'a i64 {
  let mut p: P = P { a: x, b: x };
  p.b = y;
  return p.a;
}
fn __logos_main() -> i32 { return 0i32; }

fn main() { std::process::exit(__logos_main() as i32); }

