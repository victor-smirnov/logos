// TWIN of tests/logos/pass/bc_0914b_ptrcoerceland_hb_v25_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14b-ptrcoerceland, program V25 — caught: refuted an arm — PROBES.md "build 1 crosskindxd + aorecvty as priced ... LEGAL REFUSED BY HAND Q01 Q11 Q15 V02 V02B V02I V20 V21 V25 V26 V27"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land14a_twins3.L1yE/V25_loop_loan_inside_body_legal.logos
fn __logos_main() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    let c: bool = v.len() as i64 > 1i64;
    let mut i: i64 = 0i64;
    while i < 2i64 {
        let e: &i64 = &v[1u64];
        v[0u64] = *e + i;
        i = i + 1i64;
    }
    return v[0u64] as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

