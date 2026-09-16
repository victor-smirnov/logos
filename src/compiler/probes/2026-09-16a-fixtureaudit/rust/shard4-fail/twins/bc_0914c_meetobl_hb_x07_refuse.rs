#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
static G: i64 = 5i64;
struct P<'a> { x: &'a i64, y: &'a i64 }
fn mk<'a>(x: &'a i64) -> P<'static> {
    return P { x: &G, y: x };
}
fn main() {}
