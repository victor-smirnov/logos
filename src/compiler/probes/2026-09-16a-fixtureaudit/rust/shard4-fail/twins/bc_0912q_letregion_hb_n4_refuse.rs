#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn foo<'a>(q: &'a mut i64) -> i64 {
    let mut v: i64 = 1i64;
    let r: &'a mut i64 = &mut v;
    return *r;
}
fn main() {}
