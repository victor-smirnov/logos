#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
// TWIN: the second, non-generic `fn keep(t: i64)` overload is DROPPED -- fn
// overloading is a Logos-only addition and would be E0428 in Rust, a different
// defect from the `'a: 'static` escape under test.
fn keep<'a>(t: &'a i64) -> i64 where 'a: 'static { return *t; }
fn f(u: &i64) -> i64 {
    return keep(u);
}
fn main() { let n = 1i64; f(&n); }
