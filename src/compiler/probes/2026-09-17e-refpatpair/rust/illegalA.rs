struct P { x: String }
fn deref2(p: &&P) -> String { let &&P { x } = p; x }
fn main() { let p = P { x: String::from("hi") }; let r: &P = &p; let pp: &&P = &r; println!("{}", deref2(pp)); }
