trait Shape { type Unit; fn unit(&self) -> Self::Unit; }
struct Tri;
impl Shape for Tri { type Unit = i64; fn unit(&self) -> i64 { return 7; } }
fn doubled(s: impl Shape<Unit = i64>) -> i64 { return s.unit() * 2; }
fn gn<S: Shape<Unit = i64>>(s: S) -> i64 { return s.unit() * 3; }
fn ret() -> impl Shape<Unit = i64> { return Tri; }
fn main() { println!("{} {} {}", doubled(Tri), gn(Tri), ret().unit() + 1); }
