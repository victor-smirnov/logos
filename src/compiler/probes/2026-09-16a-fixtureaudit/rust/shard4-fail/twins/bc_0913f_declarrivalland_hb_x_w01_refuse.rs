#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Tr { fn t(self: &Self) -> i64; }
impl Tr for i64 { fn t(self: &Self) -> i64 { return *self; } }
trait Q { fn q(self: &Self) -> i64; }
struct S { }
impl Q for S
where T: Tr
{
  fn q(self: &Self) -> i64 { return 3i64; }
}
fn main() {}
