#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct A<'a> { x: &'a i64 }
fn newa<'q>(x: &'q i64) -> A<'q> { return A { x: x }; }
fn foo<'a>(out: &mut A<'a>) {
    let v: i64 = 1i64;
    *out = newa(&v);
}
fn main() {}
