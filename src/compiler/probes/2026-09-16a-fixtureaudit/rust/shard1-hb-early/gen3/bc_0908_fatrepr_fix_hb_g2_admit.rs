// TWIN of tests/logos/pass/bc_0908_fatrepr_fix_hb_g2_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-08-fatrepr-fix, program G2 — caught: verdict moved base -> landed — RESULT.md "CLOSED (REFUSED -> rc 0, compiled + linked + RUN, answer checked): G1 ... G8"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-08-fatrepr-fix/hand/G2.logos
// G2 LEGAL — the MUTABLE unsize `&mut [i64;3]` -> `&mut [i64]` in a payload,
// written THROUGH the binder and read back from the array afterwards.
enum E { N, S(&mut [i64]) }
fn __logos_main() -> i32 {
    let mut a: [i64; 3] = [1i64, 2i64, 3i64];
    let e: E = E::S(&mut a);
    match e {
        E::N => { return 1i32; }
        E::S(s) => { s[1] = 42i64; }
    }
    if a[1] != 42i64 { return 10i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

