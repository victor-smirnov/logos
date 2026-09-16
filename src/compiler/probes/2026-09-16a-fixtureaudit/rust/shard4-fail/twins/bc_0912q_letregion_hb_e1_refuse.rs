#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct A<'a> { x: &'a i64 }
impl<'q> A<'q> {
    fn newa<T>(x: &'q i64, y: T) -> A<'q> {
        return A { x: x };
    }
}
fn foo<'a>() {
    let v: i64 = 22i64;
    let x: A<'a> = A::newa(&v, 22i64);
}
fn main() {}
