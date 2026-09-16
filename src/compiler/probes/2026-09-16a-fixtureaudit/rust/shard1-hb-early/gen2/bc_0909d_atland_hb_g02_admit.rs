// TWIN of tests/logos/pass/bc_0909d_atland_hb_g02_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09d-atland, program g02 — caught: verdict moved base -> landed — tables/hand_head.txt vs hand_final.txt: run=1 or cc=1 on HEAD -> cc=0 run=0 landed; PROBES.md "ELEVEN of fifteen were already wrong before a line w...
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-09d-atland/hand/g02.logos
// At on a struct FIELD whose sub-pattern is itself a STRUCT pattern.
struct Inner { a: i64 }
struct Outer { i: Inner, y: i64 }
fn __logos_main() -> i32 {
    let o: Outer = Outer { i: Inner { a: 4i64 }, y: 2i64 };
    let mut out: i64 = 0i64;
    match o {
        Outer { i: s @ Inner { .. }, y } => { out = s.a + y; }
    }
    if out != 6i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

