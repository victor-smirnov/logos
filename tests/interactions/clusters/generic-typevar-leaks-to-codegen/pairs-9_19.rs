fn doubled(it: impl Iterator<Item = i64>) -> impl Iterator<Item = i64> { it.map(|x| x * 2) }
fn main() { let mut d = doubled(0..3i64); println!("{:?}", d.next()); }
