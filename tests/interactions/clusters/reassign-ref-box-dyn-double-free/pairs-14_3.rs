trait Metric { fn v(&self) -> i64; }
struct M { x: i64 }
impl Metric for M { fn v(&self) -> i64 { self.x } }
fn main() { let mut ms: Vec<Box<dyn Metric>> = Vec::new(); ms.push(Box::new(M { x: 1 })); ms.push(Box::new(M { x: 7 })); let mut b = &ms[0]; b = &ms[1]; println!("{}", b.v()); }
