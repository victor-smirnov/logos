// TWIN of tests/logos/pass/bc_0909sig_hb_c21_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.std.fmt;` dropped (prelude in Rust)
// TWIN: self: &T -> &self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09sig, program c21 — caught: refuted an arm — PROBES.md "REFUSED AND LEGAL RUST — a live over-refusal shipped by the 09-07b landing: c14 ... c21"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/census/c21_lt_trait_named_impl_elided.logos
struct S { v: i64 }
trait T { fn g<'a>(self: &'a Self) -> &'a i64; }
impl T for S { fn g(&self) -> &i64 { return &self.v; } }
fn __logos_main() -> i32 {
    let s: S = S { v: 5i64 };
    println!("r={}", *s.g());
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

