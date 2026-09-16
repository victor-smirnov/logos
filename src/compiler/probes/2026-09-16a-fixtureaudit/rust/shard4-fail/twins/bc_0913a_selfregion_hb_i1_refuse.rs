#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Foo<'s> { r: &'s i64 }
impl<'s> Foo<'s> {
    fn swap_in(self: &Self, x: &i64) -> Self { return Self { r: x }; }
}
fn main() {}
