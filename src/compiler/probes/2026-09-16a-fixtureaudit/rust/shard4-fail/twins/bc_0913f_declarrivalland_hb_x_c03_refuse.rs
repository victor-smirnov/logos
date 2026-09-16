#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
enum Color { Red, Blue }
fn lmain() -> i32 {
  let c = Color::Red::<'static>;
  return match c { Color::Red => 1i32, Color::Blue => 2i32 };
}
fn main() {}
