fn total<I: Iterator<Item = i64>>(it: I) -> i64 { let mut s = 0i64; for v in it { s += v; } return s; }
fn main() { println!("{}", total(vec![1i64, 2, 3].into_iter())); }
