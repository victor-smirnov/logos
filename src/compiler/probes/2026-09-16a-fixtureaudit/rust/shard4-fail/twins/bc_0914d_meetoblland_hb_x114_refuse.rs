#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
impl<'a> P<'a> {
    fn second(self: &P<'a>) -> &'a i64 { return self.y; }
}
fn go<'a, 'b>(x: &'a i64, y: &'b i64) -> &'a i64 {
    let p = P { x: x, y: y };
    return p.second();
}
fn main() {}
