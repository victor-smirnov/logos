#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Tr { fn t(self: &Self) -> i64; }
struct W<H: Tr> { h: H }
impl<H: Tr> W<H> {
  fn m(self: &Self) -> i64
  where T: Tr
  {
    return 1i64;
  }
}
fn main() {}
