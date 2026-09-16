#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct H<'x> { r: &'x mut (i64, i64) }
fn lmain() -> i32 {
    let mut s1: (i64, i64) = (1i64, 2i64);
    let mut s2: (i64, i64) = (3i64, 4i64);
    let mut h: H = H { r: &mut s1 };
    let a: &mut i64 = &mut h.r.0;
    h.r = &mut s2;
    s1.0 = 7i64;
    *a = 9i64;
    return h.r.1 as i32;
}
fn main() {}
