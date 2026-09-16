#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
struct W<'a> { p: P<'a> }
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> W<'a> {
    return W { p: P { x: x, y: y } };
}
fn main() {}
