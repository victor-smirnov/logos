const NAMES: [&str; 2] = ["a", "bc"];
fn main() { let mut v: Vec<&str> = Vec::new(); for n in NAMES.iter() { v.push(*n); } println!("{} {}", v.len(), v[1]); }
