struct P { s: String }
fn main() { let a = P { s: String::from("x") }; let v = vec![a, P { s: String::from("y") }]; let w = vec![String::from("a"), String::from("b")]; println!("{} {}", v.len(), w.len()); }
