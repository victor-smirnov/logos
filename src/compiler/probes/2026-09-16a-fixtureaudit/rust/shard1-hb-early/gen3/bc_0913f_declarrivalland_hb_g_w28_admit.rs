// TWIN of tests/logos/pass/bc_0913f_declarrivalland_hb_g_w28_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: &T -> &self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-13f-declarrivalland, program G_w28 — caught: refuted an arm — PROBES.md "On the recommended form (rtunion's arms as mechanisms) ... FOUR LEGAL PROGRAMS REFUSED: G_w01 ... G_w22 ... G_w28 ... G_k26"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land0913f.KArC/selfhdr.AOvJ/G_w28_inherent_noparams_impl_where_self.logos
trait Tr {
  fn t(&self) -> i64;
}
struct S { k: i64 }
impl Tr for S {
  fn t(&self) -> i64 { return self.k; }
}
impl S
where Self: Tr
{
  fn twice(&self) -> i64 { return self.t() * 2i64; }
}
fn __logos_main() -> i32 {
  let s = S { k: 5i64 };
  return s.twice() as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

