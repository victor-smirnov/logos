trait B { fn n(&self) -> i64; }
struct X(i64);
impl B for X { fn n(&self) -> i64 { self.0 } }
fn main() {
    let v: Vec<Box<dyn B>> = vec![Box::new(X(7))];
    std::process::exit(v[0].n() as i32);
}
