use std::cell::Cell;
thread_local!(static N: Cell<i64> = Cell::new(0));
struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { N.with(|n| n.set(n.get()*10 + self.v)); } }
fn rd() -> i64 { N.with(|n| n.get()) }
fn g() -> i64 { let t: (D, i64) = (D{v:2}, 3); match t { y @ (_, _) => { return y.1; } } }
fn main() { let k = g(); println!("k={} n={}", k, rd()); }
