trait A { fn a(&self) -> i64; }
struct S(i64);
impl A for S { fn a(&self) -> i64 { self.0 } }
fn show(x: &dyn A) -> i64 { x.a() }
fn main() { let b: Box<dyn A> = Box::new(S(3)); println!("{}", show(b.as_ref())); }
