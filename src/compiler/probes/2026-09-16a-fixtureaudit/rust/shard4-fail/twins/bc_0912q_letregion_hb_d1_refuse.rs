#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct A<'a> { x: &'a i64 }
fn make<'a, T>(x: &'a i64, y: T) -> A<'a> { return A { x: x }; }
fn foo<'a>() {
    let v: i64 = 22i64;
    let x: A<'a> = make(&v, &v);
}
fn main() {}
