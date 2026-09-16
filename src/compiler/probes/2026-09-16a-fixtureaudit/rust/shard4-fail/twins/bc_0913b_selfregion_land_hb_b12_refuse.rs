#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'s> { A(&'s i64), N }
impl<'q> E<'q> {
    fn n<'a>(x: &'a i64) -> i64 {
        let e: E<'a> = Self::N;
        return *x;
    }
}
fn main() {}
