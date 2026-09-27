fn sized<const N: usize>() -> usize { N * 3 }
fn main() { println!("{} {}", sized::<5>(), sized::<100>()); }
