#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Tr { fn t(self: &Self) -> i64; }
impl Tr for i64 { fn t(self: &Self) -> i64 { return *self; } }
struct S { }
impl S {
  fn make() -> i64
  where Zed: Tr
  {
    return 1i64;
  }
}
fn main() {}
