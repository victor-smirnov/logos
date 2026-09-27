fn sized<const N: usize>() -> usize { N * 10 + 2 }
fn main() { println!("{}", sized::<3>()); }
