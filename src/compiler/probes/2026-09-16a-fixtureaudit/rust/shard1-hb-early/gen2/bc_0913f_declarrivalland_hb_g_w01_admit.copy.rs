// TWIN of tests/logos/pass/bc_0913f_declarrivalland_hb_g_w01_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: &T -> &self
// TWIN: A16 VARIANT: #[derive(Clone, Copy)] added to 1 non-Drop struct(s)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-13f-declarrivalland, program G_w01 — caught: refuted an arm — PROBES.md "On the recommended form (rtunion's arms as mechanisms) ... FOUR LEGAL PROGRAMS REFUSED: G_w01 ... G_w22 ... G_w28 ... G_k26"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land0913f.KArC/hbfix_whtraitdecl.qRvF/G_w01_noparam_method_ref_subject.logos
trait Show {
  fn show(&self) -> i64;
}
impl<'a> Show for &'a i64 {
  fn show(&self) -> i64 { return **self + 1i64; }
}
#[derive(Clone, Copy)]
struct W<T> { v: T }
impl<T> W<T> {
  fn get(&self) -> i64
  where &T: Show
  {
    return 5i64;
  }
}
fn __logos_main() -> i32 {
  let w = W { v: 3i64 };
  return w.get() as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

