// TWIN of tests/logos/pass/bc_0908_fatrepr_fix_hb_g5_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-08-fatrepr-fix, program G5 — caught: verdict moved base -> landed — RESULT.md "CLOSED (REFUSED -> rc 0, compiled + linked + RUN, answer checked): G1 ... G8"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-08-fatrepr-fix/hand/G5.logos
// G5 LEGAL — CLOSURE -> FN-POINTER in an enum payload. This coercion is a
// SECOND member of the mask the fix installs, not the one the row names, so it
// is the shape that says whether the fix is the position or the flag.
enum E { N, S(fn(i64) -> i64) }
fn __logos_main() -> i32 {
    let e: E = E::S(|x: i64| -> i64 { return x + 1i64; });
    match e {
        E::N => { return 1i32; }
        E::S(f) => { if f(41i64) != 42i64 { return 10i32; } }
    }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

