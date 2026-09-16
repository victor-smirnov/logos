#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn foo<'a>() {
    let v: i64 = 22i64;
    let x: &'a i64 = &v;
}
fn main() {}
