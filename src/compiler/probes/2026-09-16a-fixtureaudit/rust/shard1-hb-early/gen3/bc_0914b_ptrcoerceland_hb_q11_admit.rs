// TWIN of tests/logos/pass/bc_0914b_ptrcoerceland_hb_q11_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14b-ptrcoerceland, program Q11 — caught: refuted an arm — PROBES.md "build 1 crosskindxd + aorecvty as priced ... LEGAL REFUSED BY HAND Q01 Q11 Q15 V02 V02B V02I V20 V21 V25 V26 V27"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land14a_twins2.okas/Q11_ref_vec_static_to_constptr_vec_shorter.logos
static G: i64 = 7i64;
fn look<'a>(v: &'a Vec<&'static i64>) -> *const Vec<&'a i64> {
    return v;
}
fn __logos_main() -> i32 {
    let mut v: Vec<&'static i64> = Vec::new();
    v.push(&G);
    let p: *const Vec<&i64> = look(&v);
    return 1i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

