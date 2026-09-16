// TWIN of tests/logos/pass/bc_0912d_slicehop_hb_l15_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: A16 VARIANT: #[derive(Clone, Copy)] added to 1 non-Drop struct(s)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-12d-slicehop, program L15 — caught: refuted an arm — PROBES.md "still refuses L04 and L15 ... the same escape deposit that refuses L04 and L15"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/R0912d/ce/L15_deref_field_from_field_read.logos
#[derive(Clone, Copy)]
struct H { a: &i64, b: &i64 }
fn f<'a>(x: &'a i64, w: &'a i64) -> &'a i64 {
  let mut h: H = H { a: x, b: w };
  {
    let r: &mut H = &mut h;
    (*r).a = (*r).b;
  }
  return h.a;
}
fn __logos_main() -> i32 { return 0i32; }

fn main() { std::process::exit(__logos_main() as i32); }

