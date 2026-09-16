#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a> { x: &'a i64, y: &'a i64 }
struct W<'a> { p: P<'a>, r: &'a i64 }
fn mk<'a, 'b: 'a, 'c>(x: &'a i64, y: &'b i64, z: &'c i64) -> W<'a> {
    let p = P { x: x, y: y };
    return W { p: p, r: z };
}
fn main() {}
