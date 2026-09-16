#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct G<'a, T> { r: &'a T, s: &'a T }
fn mk<'a, 'b, T>(x: &'a T, y: &'b T) -> G<'a, T> {
    return G { r: x, s: y };
}
fn main() {}
