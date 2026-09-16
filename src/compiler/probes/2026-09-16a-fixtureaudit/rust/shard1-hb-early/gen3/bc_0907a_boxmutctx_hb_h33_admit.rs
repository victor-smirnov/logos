// TWIN of tests/logos/pass/bc_0907a_boxmutctx_hb_h33_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-07a-boxmutctx, program h33 — caught: refuted an arm — PROBES.md "h04 `**bb = 6` and h33 `**bb += 6` close under NEITHER half"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-07a-boxmutctx/hand/h33_boxbox_compound.logos
fn __logos_main() -> i32 {
    let mut bb: Box<Box<i64>> = Box::new(Box::new(1i64));
    **bb += 6i64;
    if **bb != 7i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

