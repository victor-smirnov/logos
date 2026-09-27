fn total(it: impl Iterator<Item = i32>) -> i32 { let mut s = 0; for v in it { s += v; } return s; }
fn main() { println!("{}", total(vec![1, 2, 3].into_iter())); }
