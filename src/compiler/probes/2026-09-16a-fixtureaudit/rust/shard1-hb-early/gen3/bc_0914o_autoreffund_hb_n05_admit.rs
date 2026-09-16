// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_n05_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14o-autoreffund, program n05 — caught: a struct-only-generic `&mut self` receiver temporary after a side effect: built first on base (216); 126 under the landing
// legality: by reading, no rustc binary
struct G<T> { v: T, w: i64, s: *mut i64 }
impl<T> Drop for G<T> { fn drop(&mut self) { unsafe { *self.s = *self.s * 10i64 + self.w; } } }
impl<T> G<T> { fn bump(&mut self) -> i64 { self.w = self.w + 5i64; return self.w; } }
fn side(s: *mut i64) -> i64 {
    unsafe { *s = *s * 10i64 + 1i64; }
    return 1i64;
}
fn mkg(s: *mut i64) -> G<i64> {
    unsafe { *s = *s * 10i64 + 2i64; }
    return G { v: 5i64, w: 1i64, s: s };
}
fn __logos_main() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let k: i64 = side(s) + mkg(s).bump();
    let got: i64 = unsafe { q };
    unsafe { print!("k={} seq={}\n", k, got); }
    if k != 7i64 { return 2i32; }
    if got != 126i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

