#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
fn go<'a>(x: &'a i64, y: &i64) -> P<'a> {
    return P { x: x, y: y };
}
fn main() {}
