#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
struct H<'a> { r: &'a i64 }
fn keeph<'a>(h: H<'a>) -> i64 where 'a: 'static { return *h.r; }
fn f(u: &i64) -> i64 {
    return keeph(H { r: u });
}
fn main() { let n = 1i64; f(&n); }
