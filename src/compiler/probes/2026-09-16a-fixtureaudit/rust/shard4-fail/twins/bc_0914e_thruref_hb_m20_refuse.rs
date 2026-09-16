#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P { a: i64, b: i64 }
fn lmain() -> i32 {
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut out: &i64 = &s2.b;
    let mut r: &P = &s2;
    {
        let s1: P = P { a: 1i64, b: 2i64 };
        r = &s1;
        out = &r.a;
        r = &s2;
    }
    return *out as i32;
}
fn main() {}
