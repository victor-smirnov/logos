// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_h26_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.lang.cmp;` dropped (prelude in Rust)
// TWIN: extern fn printf -> unsafe extern "C" block
// TWIN: self: &mut T -> &mut self
// TWIN: impl Eq{fn eq} -> impl PartialEq{fn eq} + marker impl Eq (Rust splits the trait; Logos Eq carries eq)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
unsafe extern "C" { fn printf(fmt: *const u8, ...) -> i32; }
// hand battery: round 2026-09-14o-autoreffund, program h26 — caught: (round 2026-09-14n) an operator in a call argument whose temporary was never dropped on base; 1001 under the landing
// legality: by reading, no rustc binary
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { return self.v > 0i64 && other.v > 0i64; } }
fn mk(v: i64, c: *mut i64) -> D { return D { v: v, c: c }; }
fn id(b: bool) -> bool { return b; }
fn body(p: *mut i64) -> i64 {
    let a: D = D { v: 1i64, c: p };
    if !id(a == D { v: 1000i64, c: p }) { return 3i64; }
    return 0i64;
}

fn __logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = body(p);
    let got: i64 = unsafe { n };
    unsafe { printf("n=%ld r=%ld\n".as_ptr(), got, r); }
    if r != 0i64 { return 2i32; }
    if got != 1001i64 { return 1i32; }
    return 0i32;
}
impl Eq for D {}


fn main() { std::process::exit(__logos_main() as i32); }

