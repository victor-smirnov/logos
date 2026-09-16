#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'s> { A(&'s i64), B }
impl E<'_> {
    fn mk(x: &i64) -> Self {
        return E::A(x);
    }
}
fn main() {}
