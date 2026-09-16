// TWIN of tests/logos/pass/bc_0906c_patclass_hb_c05_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.collections.vec;` dropped (prelude in Rust)
// TWIN: self: &mut T -> &mut self
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06c-patclass, program c05 — caught: verdict moved base -> landed — PROBES.md "06c hand set on m2/m4: every (c)/(d) shape at Rust's count" vs 06c HEAD counts PROBES.md-23683
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06c-patclass/hand/c05_whilelet_mut_reassign.logos
struct S { v: i64, i: *mut i64 }
impl Drop for S { fn drop(&mut self) { unsafe { *self.i = *self.i + self.v; } } }
fn eat(s: S) -> i64 { return s.v; }
fn __logos_main() -> i32 {
    let mut c: i64 = 0i64;
    let p: *mut i64 = &mut c;
    {
        let mut vs: Vec<S> = Vec::new();
        vs.push(S { v: 1i64, i: p });
        while let Some(mut s) = vs.pop() { s = S { v: 2i64, i: p }; s.v = s.v + 0i64; }
    }
    return c as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

