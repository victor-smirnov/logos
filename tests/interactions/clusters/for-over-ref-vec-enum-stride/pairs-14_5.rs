enum E { A(i64), B(i64, i64) }
fn f(e: &E) -> i64 { match e { E::A(x) => *x, E::B(x, y) => x * 10 + y } }
fn main() { let v: Vec<E> = vec![E::A(1), E::B(2, 3), E::A(4)]; for s in &v { println!("{}", f(s)); } }
