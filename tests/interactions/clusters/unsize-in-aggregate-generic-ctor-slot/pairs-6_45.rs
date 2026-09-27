trait N { fn e(&self) -> i64; }
struct A; struct B;
impl N for A { fn e(&self) -> i64 { 1 } }
impl N for B { fn e(&self) -> i64 { 2 } }
fn main() { let v: Vec<Box<dyn N>> = vec![Box::new(A), Box::new(B)]; let mut s = 0; for x in v.iter() { s += x.e(); } println!("{}", s); }
