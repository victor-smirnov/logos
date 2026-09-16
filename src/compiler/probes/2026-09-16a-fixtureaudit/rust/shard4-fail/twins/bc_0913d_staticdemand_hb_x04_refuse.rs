#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static V: i64 = 2i64;
fn both<'a>(x: &'a i64, y: &'a i64) -> i64 where 'a: 'static { return *x + *y; }
fn f(u: &i64) -> i64 {
    return both(&V, u);
}
fn main() { let n = 1i64; f(&n); }
