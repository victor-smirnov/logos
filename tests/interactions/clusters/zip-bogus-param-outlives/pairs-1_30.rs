fn pairs<A: Copy, B: Copy>(a: &[A], b: &[B]) -> Vec<(A, B)> { a.iter().zip(b.iter()).map(|(x, y)| (*x, *y)).collect() }
fn main() { let p = pairs(&[1i32, 2, 3], &[10i64, 20]); println!("{}", p.len()); println!("{} {}", p[1].0, p[1].1); }
