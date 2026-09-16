#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'a> { N, Three(&'a i64, i64, &'a i64) }
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> E<'a> {
    return E::Three(y, 1i64, x);
}
fn main() {}
