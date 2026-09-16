#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
trait Tr { fn t(self: &Self) -> i64; }
fn f() -> i64
where Self: Tr
{
  return 1i64;
}
fn main() { f(); }
