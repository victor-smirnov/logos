#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'a> { N, Two(&'a str, &'a str) }
fn mk<'a, 'b>(x: &'a str, y: &'b str) -> E<'a> {
    return E::Two(x, y);
}
fn main() {}
