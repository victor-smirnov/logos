// TWIN of tests/logos/pass/bc_0913d_staticdemand_hb_r04_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-13d-staticdemand, program r04 — caught: refuted an arm — PROBES.md "stmutdeclx illegal closed y02 y03 y04 y05 y06 · REFUSES LEGAL r04 ... r08 ... r17 ... CONDEMNED 3"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/land0913d.kjFy/hand1/r04_pick_mixed.logos
static FOO: u8 = 4u8;
static SR: &u8 = &FOO;
fn pick<'a>(x: &'a u8, y: &'a u8) -> &'a u8 { if *x > *y { return x; } return y; }
fn __logos_main() -> i32 {
    let n: u8 = 9u8;
    let z = pick(SR, &n);
    return *z as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

