#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Named {
  const N: Option<&'static str>;
}
struct S { }
impl<'a> Named for S {
  const N: Option<&'a str> = None;
}
fn main() {}
