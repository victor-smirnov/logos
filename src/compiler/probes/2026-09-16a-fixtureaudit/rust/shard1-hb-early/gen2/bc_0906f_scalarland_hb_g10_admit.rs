// TWIN of tests/logos/pass/bc_0906f_scalarland_hb_g10_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06f-scalarland, program g10 — caught: verdict moved base -> landed — PROBES.md "20 of 20 g-programs refused at base; NINETEEN now compile and run 0" (g08 -> run 139, PROBES.md)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06f-scalarland/hand/g10.logos
fn __logos_main() -> i32 {
    let mut v: i64 = 0i64;
    let mut n: i64 = 0i64;
    while let 0i64 = &v {
        n = n + 1i64;
        v = 1i64;
    }
    if n != 1i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

