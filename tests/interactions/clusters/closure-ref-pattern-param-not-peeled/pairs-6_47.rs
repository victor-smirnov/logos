fn sq(x: i64) -> i64 { x * x }
fn main() { let xs = [3i64, 4]; let v: Vec<i64> = xs.iter().map(|&x| sq(x)).collect(); println!("{:?}", v); }
