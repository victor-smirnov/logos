fn main() { let c = loop { let s = String::from("hello"); break s.len(); }; println!("{}", c); }
