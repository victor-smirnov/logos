// TWIN of tests/logos/pass/bc_0906c_patclass_hb_c02_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: &mut T -> &mut self
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06c-patclass, program c02 — caught: refuted an arm — PROBES.md "ifletmv alone turns c02 (read-only if-let) 1 -> 0 (LEAK) ... ifletdr alone turns c02 1 -> 2 and c13 1 -> 7 (DOUBLE)"; PROBES.md "c03 2 -...
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06c-patclass/hand/c02_iflet_readonly.logos

struct S { v: i64, i: *mut i64 }
impl Drop for S { fn drop(&mut self) { unsafe { *self.i = *self.i + self.v; } } }
fn eat(s: S) -> i64 { return s.v; }
fn __logos_main() -> i32 {
    let mut c: i64 = 0i64;
    let p: *mut i64 = &mut c;
    {
        let o: Option<S> = Some(S { v: 1i64, i: p });
        let mut k: i64 = 0i64;
        if let Some(r) = o { k = r.v; }
        if k != 1i64 { return 99i32; }
    }
    return c as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

