enum E { A(i64), B(i64, i64) }
fn main() { let mut out: Vec<E> = Vec::new(); out.push(E::A(1)); out.push(E::B(2, 3)); out.push(E::A(4));
  let mut n = 0; for m in &out { n += 1; match m { E::A(x) => print!("A{} ", x), E::B(x, y) => print!("B{}{} ", x, y) } } println!("{}", n); }
