fn k(s: &str) -> usize { s.len() }
fn main() { let xs = ["a", "bb"]; let mut t = 0; for x in &xs { t += k(x); } println!("{}", t); }
