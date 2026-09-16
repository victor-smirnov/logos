// TWIN of tests/logos/pass/bc_0909elide_hb_e1_param_only_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: &T -> &self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09elide, program e1_param_only — caught: verdict moved base -> landed — PROBES.md "e1_param_only ... rc 1 -> rc 0, links, RUNS 0" (e2 e3 e6 same)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/ce/e1_param_only.logos
// trait elides a NON-receiver param, impl names it. Legal Rust.
struct R { v: i64 }
trait T {
    fn f(&self, x: &i64) -> i64;
}
impl T for R {
    fn f<'a, 'b>(self: &'a R, x: &'b i64) -> i64 {
        return self.v + *x;
    }
}
fn __logos_main() -> i32 {
    let r: R = R { v: 40i64 };
    let q: i64 = 2i64;
    if r.f(&q) != 42i64 { return 1; }
    return 0;
}

fn main() { std::process::exit(__logos_main() as i32); }

