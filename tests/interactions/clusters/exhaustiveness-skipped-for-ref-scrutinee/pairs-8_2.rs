enum G { A(i32), B(i32), C }
fn f(g: &G) -> i32 { match g { G::A(x) => *x, G::B(y) => *y + 100 } }
fn main() { println!("{}", f(&G::C)); }
