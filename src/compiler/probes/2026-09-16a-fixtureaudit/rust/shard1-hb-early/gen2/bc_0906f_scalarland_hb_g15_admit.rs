// TWIN of tests/logos/pass/bc_0906f_scalarland_hb_g15_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06f-scalarland, program g15 — caught: verdict moved base -> landed — PROBES.md "20 of 20 g-programs refused at base; NINETEEN now compile and run 0" (g08 -> run 139, PROBES.md)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06f-scalarland/hand/g15.logos
fn __logos_main() -> i32 {
    let v: i64 = 4i64;
    let r: &i64 = &v;
    let rr: &&i64 = &r;
    let mut out: i64 = 0i64;
    match rr {
        4i64 => { out = 1i64; }
        _ => { out = 9i64; }
    }
    if out != 1i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

