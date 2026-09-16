// TWIN of tests/logos/pass/bc_0911b_e0207_land_hb_n10_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: T -> self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-11b-e0207-land, program n10 — caught: refuted an arm — PROBES.md "THREE LEGAL PROGRAMS WERE REFUSED: n1 ... n10 ... n11" (the first landing form)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/e0207/n10.logos
// LEGAL, and THE SHAPE A NAIVE ARM MISSES: 'a is constrained through a trait
// argument's NESTED type (`Tr2<W<'a>>`), not as a bare lifetime at trait-arg
// position. A rule that only reads LIFETIME_PARAM items directly under the
// trait's TYPE_PARAMS refuses this legal impl.
struct S { n: i64 }
struct W<'a> { r: &'a i64 }
trait Tr2<A> { type Item; fn go(self) -> i64; }
impl<'a> Tr2<W<'a>> for S {
    type Item = &'a i64;
    fn go(self) -> i64 { return 10i64; }
}
fn __logos_main() -> i32 { return 0i32; }

fn main() { std::process::exit(__logos_main() as i32); }

