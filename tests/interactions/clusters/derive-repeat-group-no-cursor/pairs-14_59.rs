#[derive(Clone, Debug, PartialEq)]
struct P { x: i64 }
fn main() { let a = P { x: 1 }; let b = a.clone(); println!("{:?} {}", b, a == b); }
