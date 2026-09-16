// TWIN of tests/logos/pass/bc_0908_fatrepr_fix_hb_g8_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-08-fatrepr-fix, program G8 — caught: verdict moved base -> landed — RESULT.md "CLOSED (REFUSED -> rc 0, compiled + linked + RUN, answer checked): G1 ... G8"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-08-fatrepr-fix/hand/G8.logos
// G8 LEGAL — a NON-ZERO argument index, with a thin arg BEFORE and AFTER the
// fat one, so a fix that only looks at arg 0 shows here.
enum E { N, S(i64, &[i64], i64) }
fn __logos_main() -> i32 {
    let a: [i64; 3] = [1i64, 2i64, 3i64];
    let e: E = E::S(11i64, &a, 22i64);
    match e {
        E::N => { return 1i32; }
        E::S(x, s, y) => { if x != 11i64 { return 10i32; } if s[2] != 3i64 { return 11i32; } if y != 22i64 { return 12i32; } }
    }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

