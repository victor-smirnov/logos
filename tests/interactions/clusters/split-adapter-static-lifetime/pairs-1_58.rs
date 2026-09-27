fn main() { let s = String::from("a//b/c"); let n = s.split('/').filter(|p| !p.is_empty()).count(); println!("{}", n); }
