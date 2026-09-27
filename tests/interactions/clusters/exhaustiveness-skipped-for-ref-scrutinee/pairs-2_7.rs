enum E { A(i64), B(Box<E>), C }
fn f(e: &E) -> i64 { let v = match e { E::A(n) => *n, E::B(b) => f(b) }; return v; }
fn main() { println!("{}", f(&E::C)); }
