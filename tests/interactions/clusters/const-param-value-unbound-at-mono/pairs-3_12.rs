fn count<const W: usize>() -> i64 { let mut n: i64 = 0; for _ in 0..W { n += 1; } return n; }
fn main() { println!("{} {}", count::<3>(), count::<5>()); }
