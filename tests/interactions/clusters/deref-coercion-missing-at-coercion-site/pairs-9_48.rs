fn len(s: &str) -> usize { s.len() }
fn main() { let items = ["ab", "cde"]; let mut t = 0; for s in items.iter() { t += len(s); } println!("{}", t); }
