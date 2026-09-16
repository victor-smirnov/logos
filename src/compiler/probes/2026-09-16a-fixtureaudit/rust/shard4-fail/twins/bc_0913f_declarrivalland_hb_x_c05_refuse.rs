#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
enum E<'a, 'b> { V(&'a i64), U(&'b i64) }
impl<'a, 'b> E<'a, 'b> {
  fn mk(r: &'a i64) -> E<'a, 'b> {
    return E::V::<'a>(r);
  }
}
fn main() {}
