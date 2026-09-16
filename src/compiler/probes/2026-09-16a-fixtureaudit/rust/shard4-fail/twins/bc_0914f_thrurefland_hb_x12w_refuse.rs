#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct H<'x> { r: &'x (i64, i64) }
fn lmain() -> i32 {
    let mut s1: (i64, i64) = (1i64, 2i64);
    let s2: (i64, i64) = (3i64, 4i64);
    let mut h: H = H { r: &s1 };
    let a: &i64 = &h.r.0;
    h.r = &s2;
    s1.0 = 9i64;
    return (*a + h.r.0) as i32;
}
fn main() {}
