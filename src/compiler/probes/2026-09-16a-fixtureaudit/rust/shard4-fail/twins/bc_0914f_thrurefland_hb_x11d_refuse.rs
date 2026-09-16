#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P { a: i64, b: i64 }
impl Drop for P { fn drop(&mut self) { } }
fn eat(p: P) -> i64 { return p.a; }
fn lmain() -> i32 {
    let s1: P = P { a: 1i64, b: 2i64 };
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut t: (&P, i64) = (&s1, 10i64);
    let a: &i64 = &t.0.a;
    t = (&s2, 20i64);
    let m: i64 = eat(s1);
    return (*a + m + t.0.a) as i32;
}
fn main() {}
