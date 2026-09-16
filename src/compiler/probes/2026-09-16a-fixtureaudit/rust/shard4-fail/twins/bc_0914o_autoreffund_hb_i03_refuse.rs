#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: `extern fn printf` dropped (unused by the checked statement).
struct D { v: i64, s: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.s = *self.s * 10i64 + self.v + 4i64; } } }
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
struct G<T> { v: T, s: *mut i64 }
impl<T> Drop for G<T> { fn drop(self: &mut G<T>) { unsafe { *self.s = *self.s * 10i64 + 6i64; } } }
impl<T> G<T> { fn peek(&self) -> &T { return &self.v; } }
fn mkg(s: *mut i64) -> G<i64> { return G { v: 5i64, s: s }; }
fn lmain() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let r: &i64 = mkg(s).peek();
    if *r != 5i64 { return 2i32; }
    return 0i32;
}
fn main() {}
