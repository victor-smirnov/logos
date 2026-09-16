#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
pub trait Eat { fn eat(self: Self) -> i64; }
struct S { v: i64 }
impl Eat for S { fn eat(self: &mut S) -> i64 { return self.v; } }
fn lmain() -> i32 { let mut s: S = S { v: 5i64 }; return s.eat() as i32; }
fn main() {}
