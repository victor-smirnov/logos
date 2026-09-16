#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum T<'a> { Leaf(&'a i64), Node(&'a i64, &'a i64), Empty }
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> T<'a> {
    return T::Node(x, y);
}
fn main() {}
