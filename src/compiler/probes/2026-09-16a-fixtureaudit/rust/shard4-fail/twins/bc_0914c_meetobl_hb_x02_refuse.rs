#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'a> { N, Two(&'a i64, &'a i64) }
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> E<'a> {
    return E::Two(x, y);
}
fn main() {}
