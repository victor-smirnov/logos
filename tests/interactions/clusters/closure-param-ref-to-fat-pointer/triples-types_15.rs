trait T { fn f(&self) -> i64; }
struct A(i64);
impl T for A { fn f(&self) -> i64 { self.0 } }
fn main() {
    let b: Box<dyn T> = Box::new(A(3));
    let r = &b;
    let c = |a: &Box<dyn T>| a.f();
    let d = |a| { let x: &Box<dyn T> = a; x.f() };
    println!("{} {}", c(r), d(r));
}
