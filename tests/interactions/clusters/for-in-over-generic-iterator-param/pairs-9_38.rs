fn total(it: impl Iterator<Item = i64>) -> i64 { let mut s = 0i64; for x in it { s += x; } s }
fn total2<I: Iterator<Item = i64>>(it: I) -> i64 { let mut s = 0i64; for x in it { s += x; } s }
fn main() { println!("{} {}", total(0..4i64), total2(0..4i64)); }
