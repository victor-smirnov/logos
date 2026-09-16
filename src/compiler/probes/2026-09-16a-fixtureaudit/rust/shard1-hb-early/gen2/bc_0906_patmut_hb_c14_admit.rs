// TWIN of tests/logos/pass/bc_0906_patmut_hb_c14_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.std.fmt;` dropped (prelude in Rust)
// TWIN: self: &mut T -> &mut self
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06-patmut, program c14 — caught: verdict moved base -> landed — PROBES.md "LEGAL, base REFUSED -> now RUN rc 0 (13): c01 ... c19"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06-patmut/hand2/c14_iflet_mut_reassign_drop_count.logos
struct S { v: i64 }
impl Drop for S { fn drop(&mut self) { println!("DROP v={}", self.v); } }
fn __logos_main() -> i32 {
    let o: Option<S> = Some(S { v: 1i64 });
    if let Some(mut s) = o {
        s = S { v: 2i64 };
        s.v = s.v + 5i64;
    }
    println!("-- end");
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

