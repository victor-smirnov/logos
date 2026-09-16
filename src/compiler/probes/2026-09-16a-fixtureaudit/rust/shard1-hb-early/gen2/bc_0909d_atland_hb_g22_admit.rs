// TWIN of tests/logos/pass/bc_0909d_atland_hb_g22_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09d-atland, program g22 — caught: refuted an arm — PROBES.md "(a) g13/g22 SEGFAULTED under the first At case"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-09d-atland/hand/g22.logos
// CONTROL for g13: `@` on the enum field, sub is a bare wildcard (no variant test).
struct Q { o: Option<i64>, y: i64 }
fn __logos_main() -> i32 {
    let q: Q = Q { o: Some(4i64), y: 2i64 };
    let mut out: i64 = 0i64;
    match q {
        Q { o: a @ _, y } => { out = y; if let Some(v) = a { out = out + v; } }
    }
    if out != 6i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

