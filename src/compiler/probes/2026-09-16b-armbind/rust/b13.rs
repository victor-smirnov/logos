use std::cell::Cell;
thread_local!(static N: Cell<i64> = Cell::new(0));
struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { N.with(|n| n.set(n.get()*10 + self.v)); } }
fn rd() -> i64 { N.with(|n| n.get()) }
struct W { a: D, b: i64 }
fn g() -> i64 { let w = W { a: D{v:2}, b: 3 }; let k = match w { y @ W { .. } => y.b }; return k; }
fn main() { let k = g(); println!("k={} n={}", k, rd()); }
