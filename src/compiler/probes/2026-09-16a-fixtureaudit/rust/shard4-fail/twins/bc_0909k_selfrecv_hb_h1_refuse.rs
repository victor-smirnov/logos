#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: `extern fn printf` dropped (unused in the program body).
struct S { v: i64, c: *mut i64 }
impl Drop for S {
    fn drop(self: S) { unsafe { *self.c = *self.c + 1i64; } }
}
fn lmain() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    { let s: S = S { v: 7i64, c: p }; }
    let got: i64 = unsafe { n };
    return got as i32;
}
fn main() {}
