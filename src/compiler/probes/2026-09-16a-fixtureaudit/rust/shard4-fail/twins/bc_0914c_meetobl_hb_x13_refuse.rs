#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct W { k: i64 }
impl W {
    fn pick2<'c>(self: &W, a: &'c i64, b: &'c i64) -> &'c i64 { return a; }
}
fn mk<'a, 'b>(w: &W, x: &'a i64, y: &'b i64) -> &'a i64 {
    return w.pick2(x, y);
}
fn main() {}
