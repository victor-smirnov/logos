#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S { v: i64, c: *mut i64 }
impl Drop for S { fn drop(self: S) { unsafe { *self.c = *self.c + 1i64; } } }
fn consume<T>(x: T) { let _y: T = x; }
fn lmain() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    consume(S { v: 1i64, c: p });
    let got: i64 = unsafe { n };
    return got as i32;
}
fn main() {}
