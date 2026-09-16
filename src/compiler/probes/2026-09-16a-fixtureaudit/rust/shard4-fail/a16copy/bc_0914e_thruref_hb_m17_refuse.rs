#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
#[derive(Clone, Copy)]
struct P { a: i64, b: i64 }
fn lmain() -> i32 {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut r: &P = &s1;
    let a: &i64 = &r.a;
    r = &s2;
    s1.a = 9i64;
    return (*a + r.a) as i32;
}
fn main() {}
