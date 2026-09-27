fn get<const W: i64>() -> i64 { let mut n: i64 = 0; for _ in 0..W { n += 1; } return n; }
fn main() { println!("{}", get::<3>()); }
