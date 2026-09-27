trait Op { fn ap(&self) -> i64; }
struct A(i64);
impl Op for A { fn ap(&self) -> i64 { self.0 } }
fn main() { let b: Vec<Box<dyn Op>> = vec![Box::new(A(1)), Box::new(A(2))]; println!("{}", b[1].ap()); }
