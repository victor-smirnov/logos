#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: `fn leak(s: *mut i64) -> &i64` has no lifetime source in Rust (E0106);
// spelled `-> &'a i64` with a free binder so the temporary-escape defect under
// test is what rustc answers.
struct D { v: i64, s: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.s = *self.s * 10i64 + self.v + 4i64; } } }
impl D {
    fn get(&self) -> i64 { return self.v; }
    fn vmut(&mut self) -> &mut i64 { return &mut self.v; }
}
struct G<T> { v: T, s: *mut i64 }
impl<T> Drop for G<T> { fn drop(self: &mut G<T>) { unsafe { *self.s = *self.s * 10i64 + 6i64; } } }
impl<T> G<T> {
    fn peek(&self) -> &T { return &self.v; }
    fn tag(&self) -> i64 { return 1i64; }
}
fn mk(v: i64, s: *mut i64) -> D {
    unsafe { *s = *s * 10i64 + v; }
    return D { v: v, s: s };
}
fn mkg(s: *mut i64) -> G<i64> {
    unsafe { *s = *s * 10i64 + 2i64; }
    return G { v: 5i64, s: s };
}
fn rd(s: *mut i64) -> i64 { return unsafe { *s }; }
fn leak<'a>(s: *mut i64) -> &'a i64 {
    return mkg(s).peek();
}
fn lmain() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let r: &i64 = leak(s);
    if *r != 5i64 { return 2i32; }
    return 0i32;
}
fn main() {}
