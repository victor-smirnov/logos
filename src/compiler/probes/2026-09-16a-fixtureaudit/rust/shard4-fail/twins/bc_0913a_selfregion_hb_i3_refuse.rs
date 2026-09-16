#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Foo<'s> { r: &'s i64 }
impl Foo<'_> {
    fn swap_in(self: &Self, x: &i64) -> Foo<'_> { return Self { r: x }; }
}
fn main() {}
