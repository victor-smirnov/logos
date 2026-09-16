#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait T2<'b> {
  const A: [&'b str; 1];
}
struct S { }
impl<'b, 'c> T2<'b> for S {
  const A: [&'c str; 1] = ["x"];
}
fn main() {}
