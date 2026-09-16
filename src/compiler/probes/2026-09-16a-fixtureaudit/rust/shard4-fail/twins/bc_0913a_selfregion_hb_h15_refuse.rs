#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'s> { A(&'s i64), B }
impl<'q> E<'q> {
    fn b<'a>(x: &'a i64) -> E<'a> {
        return Self::B;
    }
}
fn main() {}
