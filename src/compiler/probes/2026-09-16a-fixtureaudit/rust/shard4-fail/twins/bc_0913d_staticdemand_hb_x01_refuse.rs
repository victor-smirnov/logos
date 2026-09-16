#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
fn keepo<'a>(o: Option<&'a i64>) -> i64 where 'a: 'static {
    match o { Some(r) => { return *r; } None => { return 0i64; } }
}
fn f(u: &i64) -> i64 {
    return keepo(Some(u));
}
fn main() { let n = 1i64; f(&n); }
