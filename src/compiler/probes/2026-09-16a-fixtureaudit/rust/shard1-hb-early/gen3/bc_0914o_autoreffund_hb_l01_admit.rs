// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_l01_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14o-autoreffund, program l01 — caught: `side(s) + *mk(2, s).me().view()`, a borrow chain through a receiver temporary: built first on base (216); 126 under the landing
// legality: by reading, no rustc binary
struct D { v: i64, s: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.s = *self.s * 10i64 + self.v + 4i64; } } }
impl D {
    fn get(&self) -> i64 { return self.v; }
    fn view(&self) -> &i64 { return &self.v; }
    fn me(&self) -> &D { return self; }
    fn opt(&self) -> Option<&i64> { return Some(&self.v); }
    fn tryget(&self) -> Option<i64> { if self.v > 2i64 { return None; } return Some(self.v); }
}
fn mk(v: i64, s: *mut i64) -> D {
    unsafe { *s = *s * 10i64 + v; }
    return D { v: v, s: s };
}
fn side(s: *mut i64) -> i64 {
    unsafe { *s = *s * 10i64 + 1i64; }
    return 1i64;
}
fn rd(s: *mut i64) -> i64 { return unsafe { *s }; }

fn __logos_main() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let k: i64 = side(s) + *mk(2i64, s).me().view();
    let got: i64 = rd(s);
    unsafe { print!("k={} seq={}\n", k, got); }
    if k != 3i64 { return 2i32; }
    if got != 126i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

