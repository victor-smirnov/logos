use std::ops::Deref;
trait Op { fn k(&self) -> i64; }
struct M(i64);
impl Op for M { fn k(&self) -> i64 { self.0 } }
fn main() { let b: Box<dyn Op> = Box::new(M(4)); let r: &dyn Op = b.as_ref(); let s: &dyn Op = b.deref(); println!("{} {}", r.k(), s.k()); }
