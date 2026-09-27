fn main() { let parts = ["a", "bc"]; let mut s = String::new(); for p in parts.iter() { s.push_str(p); } println!("{}", s); }
