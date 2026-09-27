trait A { fn a(&self) -> i64; }
impl A for i64 { fn a(&self) -> i64 { *self + 1 } }
fn d(x: &impl A) -> i64 { x.a() }
fn main() { println!("{}", d(&4i64)); }
