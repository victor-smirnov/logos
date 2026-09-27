#[derive(Clone, Copy, PartialEq, PartialOrd)]
struct Q { n: i64, d: i64 }
fn main() { let a = Q { n: 1, d: 2 }; let b = Q { n: 1, d: 3 }; println!("{} {}", a < b, b > a); }
