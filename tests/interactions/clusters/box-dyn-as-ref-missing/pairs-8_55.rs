


trait Op { fn k(&self) -> i64; }
struct M(i64);
impl Op for M { fn k(&self) -> i64 { self.0 } }
fn use_dyn(o: &dyn Op) -> i64 { o.k() }
fn main() { let b: Box<dyn Op> = Box::new(M(4)); println!("{}", use_dyn(b.as_ref())); }
