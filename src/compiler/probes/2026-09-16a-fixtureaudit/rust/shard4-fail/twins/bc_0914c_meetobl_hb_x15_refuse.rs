#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> Option<P<'a>> {
    return Some(P { x: x, y: y });
}
fn main() {}
