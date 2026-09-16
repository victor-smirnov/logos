#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'a> { N, One(&'a str) }
fn mk<'a, 'b>(y: &'b str) -> E<'a> {
    return E::One(y);
}
fn main() {}
