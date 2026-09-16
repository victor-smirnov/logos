// TWIN of tests/logos/pass/bc_0911b_e0207_land_hb_n1_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: T -> self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-11b-e0207-land, program n1 — caught: refuted an arm — PROBES.md "THREE LEGAL PROGRAMS WERE REFUSED: n1 ... n10 ... n11" (the first landing form)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/e0207/n1.logos
// LEGAL, and DOOR 2: 'a appears only at a TRAIT-ARGUMENT position. sema_decl
// discards LIFETIME_PARAM there by design, so without carrying the fact the
// E0207 arm refuses this legal impl.
struct S { n: i64 }
trait Tr3<'x> { type Item; fn go(self) -> i64; }
impl<'a> Tr3<'a> for S {
    type Item = &'a i64;
    fn go(self) -> i64 { return 1i64; }
}
fn __logos_main() -> i32 { return 0i32; }

fn main() { std::process::exit(__logos_main() as i32); }

