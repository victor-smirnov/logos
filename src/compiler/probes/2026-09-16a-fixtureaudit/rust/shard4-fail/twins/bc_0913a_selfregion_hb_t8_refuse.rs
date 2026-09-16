#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'s> { A(&'s i64), B }
impl<'q> E<'q> {
    fn g<'a>(x: &'a i64) -> i64 {
        let s: i64 = Self::A(x);
        return 0i64;
    }
}
fn main() {}
