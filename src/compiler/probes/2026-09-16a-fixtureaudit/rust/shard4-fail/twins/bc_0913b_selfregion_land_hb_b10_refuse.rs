#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'s> { A(&'s i64), B }
trait Mk { fn mk(x: &i64) -> Self; }
impl Mk for E<'_> {
    fn mk(x: &i64) -> Self {
        return Self::A(x);
    }
}
fn main() {}
