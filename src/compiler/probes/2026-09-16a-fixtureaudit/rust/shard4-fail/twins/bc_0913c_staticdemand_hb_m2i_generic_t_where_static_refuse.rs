#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
fn keep<'a, T>(t: &'a T) -> &'static T where 'a: 'static { return t; }
fn error(u: &i64) {
    let r = keep(u);
}
fn main() { let n = 1i64; error(&n); }
