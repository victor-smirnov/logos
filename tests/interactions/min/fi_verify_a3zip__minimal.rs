fn pairs<B: Copy>(a: &[i32], b: &[B]) -> usize { a.iter().zip(b.iter()).count() }
fn main() { println!("{}", pairs(&[1i32, 2, 3], &[10i64, 20])); }
