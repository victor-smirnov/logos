#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P { a: i64, b: i64 }
fn lmain() -> i32 {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut t: (&P, i64) = (&s1, 10i64);
    let a: &i64 = &t.0.a;
    t = (&s2, 20i64);
    s1.a = 9i64;
    return (*a + t.0.a) as i32;
}
fn main() {}
