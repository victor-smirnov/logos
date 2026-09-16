#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
#[derive(Clone, Copy)]
struct P { a: i64, b: i64 }
fn lmain() -> i32 {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let mut r: &mut P = &mut s1;
    let a: &mut i64 = &mut r.a;
    r = r;
    *a = 7i64;
    return 0i32;
}
fn main() {}
