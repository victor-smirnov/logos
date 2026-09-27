trait Metric { fn v(&self) -> i64; }
struct M { x: i64 }
impl Metric for M { fn v(&self) -> i64 { self.x } }
fn main() { let b0: Box<dyn Metric> = Box::new(M { x: 1 }); let b1: Box<dyn Metric> = Box::new(M { x: 7 }); let mut b = &b0; b = &b1; println!("{}", b.v()); }
