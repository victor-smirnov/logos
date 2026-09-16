#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Named {
  const N: &'static str;
}
struct S { }
impl<'a> Named for S {
  const N: &'a str = "x";
}
fn main() {}
