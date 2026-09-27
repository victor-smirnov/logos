trait B { fn n(&self) -> i64; }
struct X(i64);
impl B for X { fn n(&self) -> i64 { self.0 } }
fn main() {
    let v: Vec<Box<dyn B>> = vec![Box::new(X(1)), Box::new(X(2))];
    for b in v.iter() { println!("{}", b.n()); }
}
