#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
#[derive(Clone, Copy)]
struct P { a: i64, b: i64 }
fn lmain() -> i32 {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let mut t: (&mut P, i64) = (&mut s1, 0i64);
    let a: &mut i64 = &mut t.0.a;
    t = (&mut s1, 1i64);
    *a = 5i64;
    return t.1 as i32;
}
fn main() {}
