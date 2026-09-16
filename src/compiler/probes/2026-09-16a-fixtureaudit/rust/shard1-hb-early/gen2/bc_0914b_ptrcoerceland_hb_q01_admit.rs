// TWIN of tests/logos/pass/bc_0914b_ptrcoerceland_hb_q01_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14b-ptrcoerceland, program Q01 — caught: refuted an arm — PROBES.md "build 1 crosskindxd + aorecvty as priced ... LEGAL REFUSED BY HAND Q01 Q11 Q15 V02 V02B V02I V20 V21 V25 V26 V27"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land14a_hand.Clf6/Q01_mutvec_static_to_shared_vec_shorter.logos
static G: i64 = 7i64;
fn look<'a>(v: &'a mut Vec<&'static i64>) -> &'a Vec<&'a i64> {
    return v;
}
fn __logos_main() -> i32 {
    let mut v: Vec<&'static i64> = Vec::new();
    v.push(&G);
    let r: &Vec<&i64> = look(&mut v);
    return r.len() as i64 as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

