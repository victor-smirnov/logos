#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Tr<'b> {
  fn get(self: &Self) -> &'b [i64];
}
struct S { }
impl<'b, 'c> Tr<'b> for S {
  fn get(self: &Self) -> &'c [i64] { return &[]; }
}
fn main() {}
