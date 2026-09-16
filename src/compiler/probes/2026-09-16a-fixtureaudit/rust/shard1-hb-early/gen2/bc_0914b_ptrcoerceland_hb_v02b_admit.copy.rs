// TWIN of tests/logos/pass/bc_0914b_ptrcoerceland_hb_v02b_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14b-ptrcoerceland, program V02B — caught: refuted an arm — PROBES.md "build 1 crosskindxd + aorecvty as priced ... LEGAL REFUSED BY HAND Q01 Q11 Q15 V02 V02B V02I V20 V21 V25 V26 V27"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land14a_twins.1LQu/V02B_store_plain_read_loan_then_dead.logos
fn __logos_main() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    let e: &i64 = &v[1u64];
    v[0u64] = *e;
    return v[0u64] as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

