// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_c09_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.lang.cmp;` dropped (prelude in Rust)
// TWIN: extern fn printf -> unsafe extern "C" block
// TWIN: self: &mut T -> &mut self
// TWIN: impl Eq{fn eq} -> impl PartialEq{fn eq} + marker impl Eq (Rust splits the trait; Logos Eq carries eq)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
unsafe extern "C" { fn printf(fmt: *const u8, ...) -> i32; }
// hand battery: round 2026-09-14o-autoreffund, program c09 — caught: an enum `==` operand temporary with a droppable payload was never dropped on base (seq 2); 26 under the landing
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

enum E { A(D), B }
impl PartialEq for E {
    fn eq(&self, other: &E) -> bool {
        match self {
            E::A(_) => { match other { E::A(_) => { return true; } _ => { return false; } } }
            _ => { return false; }
        }
    }
}
fn mke(s: *mut i64) -> E { return E::A(mk(2i64, s)); }
fn __logos_main() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let mut got: i64 = 0i64;
    {
        let e: E = E::A(D { v: 3i64, s: s });
        if !(e == mke(s)) { return 3i32; }
        got = rd(s);
    }
    unsafe { printf("seq=%ld\n".as_ptr(), got); }
    if got != 26i64 { return 1i32; }
    return 0i32;
}
impl Eq for D {}

impl Eq for E {}


fn main() { std::process::exit(__logos_main() as i32); }

