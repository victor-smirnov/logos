enum S { A(i64), B }
fn f(s: &S) -> i64 { match s { S::A(_) => 2 } }
fn main() { println!("{}", f(&S::A(5))); let _ = S::B; }
