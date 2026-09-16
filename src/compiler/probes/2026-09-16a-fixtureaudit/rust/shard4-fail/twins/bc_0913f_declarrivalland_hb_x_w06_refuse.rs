#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Tr { fn t(self: &Self) -> i64; }
impl Tr for i64 { fn t(self: &Self) -> i64 { return *self; } }
trait Q {
  fn q<U>(self: &Self, u: &U) -> i64
  where T: Tr;
}
struct S { }
impl Q for S {
  fn q<U>(self: &Self, u: &U) -> i64 { return 1i64; }
}
fn main() {}
