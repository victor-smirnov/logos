fn fill(v: &mut Vec<String>) { v.push(String::from("a")); }
fn main() { let mut v = Vec::new(); fill(&mut v); println!("{}", v.len()); }
