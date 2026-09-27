fn len(s: &str) -> i64 { s.len() as i64 }
fn main() { let xs = ["ab", "cde"]; for x in xs.iter() { println!("{}", len(x)); } let y: &&str = &"q"; println!("{}", len(y)); }
