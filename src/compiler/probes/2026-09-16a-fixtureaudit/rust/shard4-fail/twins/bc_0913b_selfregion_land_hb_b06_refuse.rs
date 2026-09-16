#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'s> { A(&'s i64), B }
impl<'q> E<'q> {
    fn mk<'a>(x: &'a i64, b: bool) -> E<'a> {
        if b {
            return Self::A(x);
        }
        return E::B;
    }
}
fn main() {}
