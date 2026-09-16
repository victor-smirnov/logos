// TWIN of tests/logos/pass/bc_0908_fatrepr_fix_hb_g4_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-08-fatrepr-fix, program G4 — caught: verdict moved base -> landed — RESULT.md "CLOSED (REFUSED -> rc 0, compiled + linked + RUN, answer checked): G1 ... G8"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-08-fatrepr-fix/hand/G4.logos
// G4 LEGAL — the ctor at a RETURN position inside a function whose result is
// matched by the caller; the array outlives the call through a parameter.
enum E { N, S(&[i64]) }
fn wrap(p: &[i64]) -> E { return E::S(p); }
fn wrap2(p: &[i64; 3]) -> E { return E::S(p); }
fn __logos_main() -> i32 {
    let a: [i64; 3] = [1i64, 2i64, 30i64];
    let e: E = wrap2(&a);
    match e {
        E::N => { return 1i32; }
        E::S(s) => { if s[2] != 30i64 { return 10i32; } }
    }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

