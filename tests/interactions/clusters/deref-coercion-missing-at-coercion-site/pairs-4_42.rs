fn n(s: &str) -> usize { s.len() }
fn main() { let w = ["ab", "cde"]; let mut t = 0; for x in w.iter() { t += n(x); } println!("{}", t); }
