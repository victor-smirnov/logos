// TWIN of tests/logos/pass/bc_0906_patmut_hb_c17_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: T -> self
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06-patmut, program c17 — caught: verdict moved base -> landed — PROBES.md "LEGAL, base REFUSED -> now RUN rc 0 (13): c01 ... c19"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06-patmut/hand2/c17_generic_impl_method_match_mut.logos
struct W<T> { o: Option<T> }
impl<T> W<T> {
    fn take_add(self, k: i64) -> i64 {
        match self.o {
            Some(mut n) => { return k; },
            None => { return 0i64; }
        }
    }
}
fn __logos_main() -> i32 {
    let w: W<i64> = W { o: Some(1i64) };
    let w2: W<bool> = W { o: Some(true) };
    if w.take_add(5i64) != 5i64 { return 1i32; }
    if w2.take_add(6i64) != 6i64 { return 2i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

