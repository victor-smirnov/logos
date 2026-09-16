// TWIN of tests/logos/pass/bc_0913f_declarrivalland_hb_g_w27_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: &T -> &self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-13f-declarrivalland, program G_w27 — caught: verdict moved base -> landed — PROBES.md "ONE BASE LEGAL REFUSAL REPAIRED by the fallback deletion: G_w27 ... landed runs 4"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land0913f.KArC/selfhdr.AOvJ/G_w27_inherent_generic_impl_where_self.logos
trait Tr {
  fn t(&self) -> i64;
}
struct W<X> { v: X }
impl<X> Tr for W<X> {
  fn t(&self) -> i64 { return 2i64; }
}
impl<X> W<X>
where Self: Tr
{
  fn twice(&self) -> i64 { return self.t() * 2i64; }
}
fn __logos_main() -> i32 {
  let w = W { v: 1i64 };
  return w.twice() as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

