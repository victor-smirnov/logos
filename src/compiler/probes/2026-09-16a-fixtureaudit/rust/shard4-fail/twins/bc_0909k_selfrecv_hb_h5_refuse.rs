#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct G<T> { v: T, c: *mut i64 }
impl<T> Drop for G<T> {
    fn drop(self: G<T>) { unsafe { *self.c = *self.c + 1i64; } }
}
fn lmain() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    { let g: G<i64> = G { v: 7i64, c: p }; }
    let got: i64 = unsafe { n };
    return got as i32;
}
fn main() {}
