#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'s> { V { r: &'s i64 }, N }
impl<'q> E<'q> {
    fn mk<'a>(x: &'a i64) -> E<'a> {
        return Self::V { r: x };
    }
}
fn main() {}
