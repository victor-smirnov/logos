// TWIN of tests/logos/pass/bc_0906e_scalarcore_hb_h35_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06e-scalarcore, program h35 — caught: refuted an arm — PROBES.md "h33 / h34 / h35 put a LITERAL or RANGE arm and a WHOLE-SCRUTINEE BINDER ... legal Rust — and under `scall` they are still refused"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06e-scalarcore/hand/h35.logos
// a range arm beside a `ref` binder arm under a reference scrutinee
fn __logos_main() -> i32 {
    let v: i64 = 7i64;
    let mut out: i64 = 0i64;
    match &v { 1i64..=5i64 => { out = 1i64; } ref r => { out = **r; } }
    if out != 7i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

