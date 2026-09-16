use std::cell::Cell;
thread_local!(static N: Cell<i64> = Cell::new(0));
struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { N.with(|n| n.set(n.get()*10 + self.v)); } }
fn rd() -> i64 { N.with(|n| n.get()) }
fn g() -> i64 { let arr: [D; 2] = [D{v:2}, D{v:3}]; let [a, b] = arr; return a.v + b.v; }
fn main() { let k = g(); println!("k={} n={}", k, rd()); }
