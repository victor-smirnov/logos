trait A { fn a(&self) -> i64; }
struct S(i64);
impl A for S { fn a(&self) -> i64 { self.0 } }
fn mk(n: i64) -> Box<dyn A> { Box::new(S(n)) }
fn main() { let v: Vec<Box<dyn A>> = vec![mk(1), mk(2)]; println!("{}", v[1].a()); }
