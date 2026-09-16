// TWIN of tests/logos/pass/bc_0913d_staticdemand_hb_e05_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-13d-staticdemand, program e05 — caught: refuted an arm — PROBES.md "cooutsitese + x08 · REFUSES LEGAL e01 ... e02 ... e05 ... e08 ... CONDEMNED 4"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land0913d.kjFy/hand/e05_const_ref.logos
const C: i64 = 7i64;
fn static_id<'a>(t: &'a i64) -> &'static i64 where 'a: 'static { return t; }
fn __logos_main() -> i32 {
    let r = static_id(&C);
    return *r as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

