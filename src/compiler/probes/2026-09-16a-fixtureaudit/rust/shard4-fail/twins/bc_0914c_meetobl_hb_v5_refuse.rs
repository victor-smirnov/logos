#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a, T> { x: &'a T, y: &'a T }
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> P<'a, i64> {
    return P::<i64> { x: x, y: y };
}
fn main() {}
