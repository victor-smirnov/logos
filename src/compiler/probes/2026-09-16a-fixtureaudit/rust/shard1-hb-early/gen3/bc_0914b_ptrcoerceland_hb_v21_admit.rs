// TWIN of tests/logos/pass/bc_0914b_ptrcoerceland_hb_v21_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: u64 index literal -> usize (Rust indices are usize)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14b-ptrcoerceland, program V21 — caught: refuted an arm — PROBES.md "build 1 crosskindxd + aorecvty as priced ... LEGAL REFUSED BY HAND Q01 Q11 Q15 V02 V02B V02I V20 V21 V25 V26 V27"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land14a_twins3.L1yE/V21_ifarm_last_use_before_store.logos
fn __logos_main() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    let c: bool = v.len() as i64 > 1i64;
    let e: &i64 = &v[1usize];
    if c {
        let x: i64 = *e;
        v[0usize] = x;
    }
    return v[0usize] as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

