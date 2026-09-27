fn sum_all<I: Iterator<Item = i64>>(it: I) -> i64 { let mut s = 0; for x in it { s += x; } s }
fn main() { let v: Vec<i64> = vec![1, 2, 3]; println!("{}", sum_all(v.into_iter())); }
