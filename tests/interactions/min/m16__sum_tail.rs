fn tot(v: &[i64]) -> i64 { v.iter().sum() }
fn main() { std::process::exit(tot(&[1, 2, 3]) as i32); }
