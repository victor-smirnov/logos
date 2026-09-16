// TWIN of tests/logos/pass/bc_0907q_dbmcarry_hb_c06_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.std.fmt;` dropped (prelude in Rust)
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-07q-dbmcarry, program c06 — caught: verdict moved base -> landed — PROBES.md "HAND MATRIX, base -> armed ... 2 -> 1 (nine): c01 ... c19" (landed)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-07q-dbmcarry/hand/c06.logos
struct S { pub n: i64 }
impl Drop for S { fn drop(&mut self) { println!("D{}", self.n); } }
struct W { pub t: (S, i64), pub k: i64 }
fn __logos_main() -> i32 {
    let w: W = W { t: (S { n: 5i64 }, 1i64), k: 9i64 };
    let mut out: i64 = 0i64;
    match &w {
        W { t: (s, m), k } => { out = s.n + *m + *k; }
    }
    if out != 15i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

