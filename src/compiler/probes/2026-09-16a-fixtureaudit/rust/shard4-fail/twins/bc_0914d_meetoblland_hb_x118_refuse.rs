#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
enum E<'a> { N, Two(P<'a>, &'a i64) }
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> E<'a> {
    return E::Two(P { x: x, y: x }, y);
}
fn main() {}
