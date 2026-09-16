// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_n02_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf -> unsafe extern "C" block
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
unsafe extern "C" { fn printf(fmt: *const u8, ...) -> i32; }
// hand battery: round 2026-09-14o-autoreffund, program n02 — caught: a struct-only-generic receiver temporary after a side effect: built first on base (216, lower_method_call's hoist); 126 under the landing
// legality: by reading, no rustc binary
struct D { v: i64, s: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.s = *self.s * 10i64 + self.v + 4i64; } } }
impl D { fn get(&self) -> i64 { return self.v; } }
fn mk(v: i64, s: *mut i64) -> D {
    unsafe { *s = *s * 10i64 + v; }
    return D { v: v, s: s };
}
fn side(s: *mut i64) -> i64 {
    unsafe { *s = *s * 10i64 + 1i64; }
    return 1i64;
}
fn rd(s: *mut i64) -> i64 { return unsafe { *s }; }

struct G<T> { v: T, s: *mut i64 }
impl<T> Drop for G<T> { fn drop(&mut self) { unsafe { *self.s = *self.s * 10i64 + 6i64; } } }
impl<T> G<T> { fn tag(&self) -> i64 { return 1i64; } }
fn mkg(s: *mut i64) -> G<i64> {
    unsafe { *s = *s * 10i64 + 2i64; }
    return G { v: 5i64, s: s };
}
fn __logos_main() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let k: i64 = side(s) + mkg(s).tag();
    let got: i64 = rd(s);
    unsafe { printf("k=%ld seq=%ld\n".as_ptr(), k, got); }
    if k != 2i64 { return 2i32; }
    if got != 126i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

