#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
struct P { x: i64 }
fn lmain() -> i32 {
  let p = P::<'static> { x: 1i64 };
  return p.x as i32;
}
fn main() {}
