// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_c16_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.lang.cmp;` dropped (prelude in Rust)
// TWIN: extern fn printf -> unsafe extern "C" block
// TWIN: self: &mut T -> &mut self
// TWIN: impl Eq{fn eq} -> impl PartialEq{fn eq} + marker impl Eq (Rust splits the trait; Logos Eq carries eq)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
unsafe extern "C" { fn printf(fmt: *const u8, ...) -> i32; }
// hand battery: round 2026-09-14o-autoreffund, program c16 — caught: an operator operand temporary in a match-expression arm was never dropped on base (seq 3); the untaken arm builds nothing; 37 under the landing
// legality: by reading, no rustc binary
struct D { v: i64, s: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.s = *self.s * 10i64 + self.v + 4i64; } } }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { return self.v > 0i64 && other.v > 0i64; } }
fn mk(v: i64, s: *mut i64) -> D {
    unsafe { *s = *s * 10i64 + v; }
    return D { v: v, s: s };
}
fn side(s: *mut i64) -> i64 {
    unsafe { *s = *s * 10i64 + 1i64; }
    return 1i64;
}
fn pick<'a>(d: &'a D, s: *mut i64) -> &'a D {
    unsafe { *s = *s * 10i64 + 1i64; }
    return d;
}
fn rd(s: *mut i64) -> i64 { return unsafe { *s }; }

fn __logos_main() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let mut got: i64 = 0i64;
    {
        let a: D = D { v: 3i64, s: s };
        let k: i64 = 1i64;
        let r1: bool = match k { 0i64 => a == mk(2i64, s), _ => false };
        let r2: bool = match k { 1i64 => a == mk(3i64, s), _ => false };
        if r1 || !r2 { return 3i32; }
        got = rd(s);
    }
    unsafe { printf("seq=%ld\n".as_ptr(), got); }
    if got != 37i64 { return 1i32; }
    return 0i32;
}
impl Eq for D {}


fn main() { std::process::exit(__logos_main() as i32); }

