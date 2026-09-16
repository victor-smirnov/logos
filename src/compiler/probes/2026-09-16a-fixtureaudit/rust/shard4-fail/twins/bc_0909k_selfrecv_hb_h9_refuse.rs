#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
pub trait Tick { fn tick(self: &mut Self) -> i64; }
struct S { v: i64 }
impl Tick for S { fn tick(self: &S) -> i64 { return self.v; } }
fn lmain() -> i32 { let mut s: S = S { v: 4i64 }; return s.tick() as i32; }
fn main() {}
