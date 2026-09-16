#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct W<'s, T> { r: &'s T }
impl<'q, T> W<'q, T> {
    fn mk<'a>(x: &'a T) -> W<'a, T> { return Self { r: x }; }
}
fn main() {}
