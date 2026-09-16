// TWIN of tests/logos/pass/bc_0912q_letregion_hb_m5_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-12q-letregion, program m5 — caught: verdict moved base -> landed — PROBES.md "Newly COMPILING, all legal, all run to their exit code: y1 y2 y3 (deref stop) · a6 a10 e3 m1 m3 m4 m5 o3"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/r0912q/ctl/m5_legal_ref_return_static.logos
struct A<'a> { x: &'a i64 }
impl<'q> A<'q> {
    fn id(x: &'q i64) -> &'q i64 {
        return x;
    }
}
fn __logos_main() -> i32 {
    let v: i64 = 23i64;
    let r: &'_ i64 = A::id(&v);
    return *r as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

