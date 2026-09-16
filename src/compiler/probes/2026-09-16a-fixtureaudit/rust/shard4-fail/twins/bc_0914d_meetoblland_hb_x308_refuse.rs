#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum E<'a> { N, One(&'a str) }
fn mk<'a>(s: &'a str) -> E<'static> { return E::One(s); }
fn main() {}
