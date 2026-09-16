#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P { k: i64 }
fn foo<'a>(q: &'a i64) -> i64 {
    let p: P = P { k: 3i64 };
    let r: &'a i64 = &p.k;
    return *r;
}
fn main() {}
