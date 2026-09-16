#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Tr { fn t(self: &Self) -> i64; }
trait Q {
  fn q(self: &Self) -> i64
  where T: Tr;
}
fn main() {}
