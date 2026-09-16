// TWIN of tests/logos/pass/bc_0909d_atland_hb_g11_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09d-atland, program g11 — caught: refuted an arm, verdict moved base -> landed — PROBES.md "(c) g03/g11 stayed run 1" (first-cut fix) | (b) tables/hand_head.txt vs hand_final.txt: run=1 or cc=1 on HEAD -> cc=0 run=0 landed; PROB...
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-09d-atland/hand/g11.logos
// A NESTED tuple as the sub-pattern of a tuple-element at-binding.
fn __logos_main() -> i32 {
    let t: ((i64, i64), i64) = ((1i64, 2i64), 3i64);
    let mut out: i64 = 0i64;
    match t {
        (p @ (a, b), c) => { out = a + b + c + p.0; }
        (_, _) => { out = -1i64; }
    }
    if out != 7i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

