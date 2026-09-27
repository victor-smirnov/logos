trait T { fn f(&self) -> i64; fn g(&self, n: i64) -> i64 { self.f() * n } }
struct A(i64);
impl T for A { fn f(&self) -> i64 { self.0 } }
fn main() {
    let mut v: Vec<Box<dyn T>> = Vec::new();
    v.push(Box::new(A(3))); v.push(Box::new(A(4)));
    let s: i64 = v.iter().map(|a| a.f()).sum();
    println!("{}", s);
}
