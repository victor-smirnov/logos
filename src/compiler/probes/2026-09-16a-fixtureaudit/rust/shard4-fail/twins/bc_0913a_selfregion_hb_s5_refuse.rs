#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Foo<'s> { r: &'s i64 }
trait Mk { fn mk(x: &i64) -> Self; }
impl Mk for Foo<'_> {
    fn mk(x: &i64) -> Self { return Foo { r: x }; }
}
fn main() {}
