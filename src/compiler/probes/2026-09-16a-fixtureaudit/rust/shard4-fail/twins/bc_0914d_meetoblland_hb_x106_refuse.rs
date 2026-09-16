#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
struct H<'a> { p: P<'a>, n: i64 }
impl<'a> H<'a> {
    fn set<'b>(self: &mut H<'a>, x: &'a i64, y: &'b i64) {
        self.p = P { x: x, y: y };
    }
}
fn main() {}
