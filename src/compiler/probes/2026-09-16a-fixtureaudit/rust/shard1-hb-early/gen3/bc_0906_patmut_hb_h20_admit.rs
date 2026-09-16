// TWIN of tests/logos/pass/bc_0906_patmut_hb_h20_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06-patmut, program h20 — caught: verdict moved base -> landed — PROBES.md "LEGAL, base REFUSED -> whole RUNS rc 0 (18)" + PROBES.md "The pricing's 42 hand shapes re-run on both builds: identical"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06-patmut/hand/h20_match_mut_addrof.logos
struct S { v: i64 }
enum E { A(S), B }
fn bump(p: &mut S) { p.v = p.v + 7i64; }
fn __logos_main() -> i32 {
    let e: E = E::A(S { v: 1i64 });
    match e {
        E::A(mut s) => { bump(&mut s); if s.v != 8i64 { return 1i32; } },
        E::B => {}
    }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

