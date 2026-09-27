


trait Op { fn k(&self) -> i64; }
struct M(i64);
impl Op for M { fn k(&self) -> i64 { self.0 } }
fn use_dyn(o: &dyn Op, n: i64) -> i64 { o.k() + n }
fn main() { let b: Box<dyn Op> = Box::new(M(4)); println!("{}", use_dyn(&*b, 2)); }
