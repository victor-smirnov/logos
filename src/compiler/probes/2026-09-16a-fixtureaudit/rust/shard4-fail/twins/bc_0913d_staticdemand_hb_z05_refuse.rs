#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
fn keep2<'a, X>(x: X, r: &'a i64) -> i64 where 'a: 'static { return *r; }
fn outer<T>(t: T, u: &i64) -> i64 {
    return keep2(t, u);
}
fn main() { let n = 1i64; outer(5i64, &n); }
