#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
trait Mk { fn mk(x: &i64) -> Self; }
impl Mk for &'_ i64 {
    fn mk(x: &i64) -> Self {
        return x;
    }
}
fn main() {}
