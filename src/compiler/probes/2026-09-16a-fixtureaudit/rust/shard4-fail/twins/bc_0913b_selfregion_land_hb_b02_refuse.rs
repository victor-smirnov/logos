#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Foo<'s> { r: &'s i64 }
impl<'q> Foo<'q> {
    fn mk<'a>(x: &'a i64) -> Option<Foo<'a>> {
        return Some(Self { r: x });
    }
}
fn main() {}
