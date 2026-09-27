trait T { fn f(&self) -> i64; }
struct A(i64);
impl T for A { fn f(&self) -> i64 { self.0 } }
fn main() {
    let v: Vec<Box<dyn T>> = vec![Box::new(A(1)), Box::new(A(2))];
    let mut w: Vec<Box<dyn T>> = Vec::new();
    w.push(Box::new(A(5)));
    println!("{} {}", v[1].f(), w[0].f());
}
