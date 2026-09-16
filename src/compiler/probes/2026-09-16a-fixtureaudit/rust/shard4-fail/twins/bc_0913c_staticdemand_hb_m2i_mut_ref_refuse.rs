#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
fn grab<'a>(t: &'a mut i64) -> &'static mut i64 where 'a: 'static { return t; }
fn error(u: &mut i64) {
    let r = grab(u);
}
fn main() { let mut n = 1i64; error(&mut n); }
