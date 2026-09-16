#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
struct W<'a> { r: &'a i64 }
fn keep<'a>(w: W<'a>) -> &'static i64 where 'a: 'static { return w.r; }
fn error(u: &i64) {
    let r = keep(W { r: u });
}
fn main() { let n = 1i64; error(&n); }
