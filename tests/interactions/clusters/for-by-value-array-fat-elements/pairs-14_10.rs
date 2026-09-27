fn main() { let a: [&str; 2] = ["ab ", "c"]; for s in a { println!("{}", s.len()); } }
