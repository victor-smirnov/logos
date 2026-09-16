use std::cell::Cell;
thread_local!(static N: Cell<i64> = Cell::new(0));
struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { N.with(|n| n.set(n.get()*10 + self.v)); } }
fn rd() -> i64 { N.with(|n| n.get()) }
fn g() -> i64 { let o: Option<D> = Some(D{v:2}); match o { y @ Some(_) => { let _ = y; return 4; } None => { return 0; } } }
fn main() { let k = g(); println!("k={} n={}", k, rd()); }
