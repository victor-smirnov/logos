// TWIN of tests/logos/pass/bc_0913d_staticdemand_hb_e01_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-13d-staticdemand, program e01 — caught: refuted an arm — PROBES.md "cooutsitese + x08 · REFUSES LEGAL e01 ... e02 ... e05 ... e08 ... CONDEMNED 4"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land0913d.kjFy/hand/e01_option_let_static.logos
static V: i64 = 5i64;
fn keepo<'a>(o: Option<&'a i64>) -> i64 where 'a: 'static {
    match o { Some(r) => { return *r; } None => { return 0i64; } }
}
fn __logos_main() -> i32 {
    let o: Option<&i64> = Some(&V);
    let k = keepo(o);
    return k as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

