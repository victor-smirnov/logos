// TWIN of tests/logos/pass/bc_0908_fatrepr_fix_hb_g6_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-08-fatrepr-fix, program G6 — caught: verdict moved base -> landed — RESULT.md "CLOSED (REFUSED -> rc 0, compiled + linked + RUN, answer checked): G1 ... G8"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-08-fatrepr-fix/hand/G6.logos
// G6 LEGAL — TWO ctors of the same variant in an ARRAY of enums, read back by
// index; the payload is a slice of a DIFFERENT length each time.
enum E { N, S(&[i64]) }
fn __logos_main() -> i32 {
    let a: [i64; 3] = [1i64, 2i64, 3i64];
    let b: [i64; 2] = [9i64, 8i64];
    let es: [E; 2] = [E::S(&a), E::S(&b)];
    let mut acc: i64 = 0i64;
    let mut i: i64 = 0i64;
    while i < 2i64 {
        match es[i] {
            E::N => { return 1i32; }
            E::S(s) => { acc = acc + s[0]; }
        }
        i = i + 1i64;
    }
    if acc != 10i64 { return 10i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

