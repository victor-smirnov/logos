#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct W<'s, T> { r: &'s T }
impl<T> W<'_, T> {
    fn mk(x: &T) -> Self {
        return Self { r: x };
    }
}
fn main() {}
