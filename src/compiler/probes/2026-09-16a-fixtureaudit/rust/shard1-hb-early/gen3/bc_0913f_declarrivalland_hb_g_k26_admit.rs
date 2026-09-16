// TWIN of tests/logos/pass/bc_0913f_declarrivalland_hb_g_k26_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: Option::Some/None -> prelude
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-13f-declarrivalland, program G_k26 — caught: refuted an arm — PROBES.md "On the recommended form (rtunion's arms as mechanisms) ... FOUR LEGAL PROGRAMS REFUSED: G_w01 ... G_w22 ... G_w28 ... G_k26"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land0913f.KArC/selfhdr.AOvJ/G_k26_const_option_elided_no_lifetimes_in_scope.logos
trait Named {
  const N: Option<&'static str>;
}
struct S { }
impl Named for S {
  const N: Option<&str> = Some("abc");
}
fn __logos_main() -> i32 {
  return match S::N { Some(s) => s.len() as i64 as i32, None => 0i32 };
}

fn main() { std::process::exit(__logos_main() as i32); }

