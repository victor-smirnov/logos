// TWIN of tests/logos/pass/bc_0906p_ergodoors_hb_n12_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.std.fmt;` dropped (prelude in Rust)
// TWIN: self: &mut T -> &mut self
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06p-ergodoors, program n12 — caught: refuted an arm — PROBES.md "`ergoref` refuses r01 r02 r06 r14 (all four 2024-illegal, correct) AND n12 — a program with no `ref` anywhere"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06p-ergodoors/hand/n12.logos
struct S { pub n: i64 }
impl Drop for S { fn drop(&mut self) { println!("D{}", self.n); } }
enum Outer { W(Option<S>), Z }
fn __logos_main() -> i32 {
    let e: Outer = Outer::W(Some(S { n: 5i64 }));
    let mut out: i64 = 0i64;
    match &e {
        Outer::W(Some(a)) => { out = a.n; },
        Outer::W(None) => {},
        Outer::Z => {}
    }
    if out != 5i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

