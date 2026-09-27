trait Producer { type Out; fn produce(&mut self) -> Self::Out; }
struct Ctr { n: i64 }
impl Producer for Ctr { type Out = i64; fn produce(&mut self) -> i64 { self.n += 2; self.n } }
fn one(p: &mut dyn Producer<Out = i64>) -> i64 { p.produce() }
fn main() { let mut c = Ctr { n: 0 }; let x = one(&mut c); println!("{}", x); }
