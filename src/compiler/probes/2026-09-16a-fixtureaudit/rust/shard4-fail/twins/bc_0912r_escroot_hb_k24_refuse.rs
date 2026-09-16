#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn idm<'q>(x: &'q mut i64) -> &'q mut i64 { return x; }
fn foo<'a>() {
    let mut v: i64 = 1i64;
    let r: &'a mut i64 = idm(&mut v);
}
fn main() {}
