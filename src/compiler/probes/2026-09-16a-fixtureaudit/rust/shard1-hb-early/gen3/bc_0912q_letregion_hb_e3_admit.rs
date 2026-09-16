// TWIN of tests/logos/pass/bc_0912q_letregion_hb_e3_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-12q-letregion, program e3 — caught: verdict moved base -> landed — PROBES.md "Newly COMPILING, all legal, all run to their exit code: y1 y2 y3 (deref stop) · a6 a10 e3 m1 m3 m4 m5 o3"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/r0912q/ctl/e3_legal_ufcs_named_let.logos
struct A<'a> { x: &'a i64 }
impl<'q> A<'q> {
    fn newa(x: &'q i64) -> A<'q> {
        return A { x: x };
    }
}
fn foo<'a>(r: &'a i64) -> i64 {
    let x: A<'a> = A::newa(r);
    return *x.x;
}
fn __logos_main() -> i32 {
    let v: i64 = 17i64;
    return foo(&v) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

