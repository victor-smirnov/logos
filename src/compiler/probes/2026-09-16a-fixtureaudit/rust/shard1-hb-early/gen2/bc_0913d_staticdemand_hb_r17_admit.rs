// TWIN of tests/logos/pass/bc_0913d_staticdemand_hb_r17_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-13d-staticdemand, program r17 — caught: refuted an arm — PROBES.md "stmutdeclx illegal closed y02 y03 y04 y05 y06 · REFUSES LEGAL r04 ... r08 ... r17 ... CONDEMNED 3"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land0913d.kjFy/hand1/r17_compare_static_local.logos
static V: i64 = 5i64;
static SR: &i64 = &V;
fn eqr<'a>(x: &'a i64, y: &'a i64) -> bool { return *x == *y; }
fn __logos_main() -> i32 {
    let n: i64 = 5i64;
    let mut hits = 0i32;
    if eqr(SR, &n) { hits = hits + 1i32; }
    if SR == &n { hits = hits + 1i32; }
    return hits;
}

fn main() { std::process::exit(__logos_main() as i32); }

