fn len(s: &str) -> usize { s.len() }
fn main() { let x = "abc"; let r = &x; println!("{}", len(r)); }
