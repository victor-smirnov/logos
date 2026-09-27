fn f(s: &str) -> usize { s.len() }
fn main() { let a = ["xy", "z"]; let mut t = 0; for s in a.iter() { t += f(s); } println!("{}", t); }
