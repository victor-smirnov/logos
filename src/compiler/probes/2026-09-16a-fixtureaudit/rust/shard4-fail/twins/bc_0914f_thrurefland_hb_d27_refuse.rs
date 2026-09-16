#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P { a: i64, b: i64 }
fn id<'x>(p: &'x mut P) -> &'x mut P { return p; }
fn lmain() -> i32 {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let mut r: &mut P = &mut s1;
    let a: &mut i64 = &mut r.a;
    r = id(r);
    *a = 5i64;
    r.b = 3i64;
    return 0i32;
}
fn main() {}
