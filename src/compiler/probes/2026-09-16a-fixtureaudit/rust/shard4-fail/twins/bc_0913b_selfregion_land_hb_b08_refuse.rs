#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct W<T> { t: T }
impl W<&'_ i64> {
    fn mk(x: &i64) -> Self {
        return W { t: x };
    }
}
fn main() {}
