#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
impl<'a> P<'a> {
    fn new(x: &'a i64, y: &'a i64) -> P<'a> { return P { x: x, y: y }; }
}
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> P<'a> {
    return P::new(x, y);
}
fn main() {}
