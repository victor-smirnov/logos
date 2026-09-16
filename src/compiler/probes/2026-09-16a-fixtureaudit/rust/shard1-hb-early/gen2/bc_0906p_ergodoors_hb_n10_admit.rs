// TWIN of tests/logos/pass/bc_0906p_ergodoors_hb_n10_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.std.fmt;` dropped (prelude in Rust)
// TWIN: self: &mut T -> &mut self
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06p-ergodoors, program n10 — caught: refuted an arm — PROBES.md "ergonest CHANGES ... NOT changed: n10 (struct door) and n11 (slice door)" (destructor count 2, Rust 1)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06p-ergodoors/hand/n10.logos
// same program (modulo package/comments) in round 2026-09-07q-dbmcarry as c04
struct S { pub n: i64 }
impl Drop for S { fn drop(&mut self) { println!("D{}", self.n); } }
struct W { pub o: Option<S>, pub k: i64 }
fn __logos_main() -> i32 {
    let w: W = W { o: Some(S { n: 5i64 }), k: 9i64 };
    let mut out: i64 = 0i64;
    match &w {
        W { o: Some(a), k } => { out = a.n + *k; },
        W { o: None, k: _ } => {}
    }
    if out != 14i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

