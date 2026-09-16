#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P2<'a, 'b> { x: &'a i64, y: &'b i64, z: &'a i64 }
fn mk<'a, 'b, 'c>(x: &'a i64, y: &'b i64, z: &'c i64) -> P2<'a, 'b> {
    return P2 { x: x, y: y, z: z };
}
fn main() {}
