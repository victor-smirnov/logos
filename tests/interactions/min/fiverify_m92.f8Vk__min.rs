enum E { A, B }
impl E { fn f(&self, k: i64) -> i64 { match self { E::A => k, E::B => 0 } } }
fn g(r: &E) -> i64 { r.f(1) }
fn main() { let x = E::A; let r = &x; println!("{} {}", r.f(1), g(&E::B)); }
