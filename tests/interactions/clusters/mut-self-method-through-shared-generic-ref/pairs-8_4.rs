trait C { fn put(&mut self, x: i64); fn get(&self) -> i64; }
struct R { v: i64 }
impl C for R { fn put(&mut self, x: i64) { self.v = x; } fn get(&self) -> i64 { self.v } }
fn g<T: C>(c: &T) { c.put(4); }
fn main() { let r = R { v: 0 }; let a = &r; let b = &r; g(a); println!("{} {}", b.get(), r.v); }
