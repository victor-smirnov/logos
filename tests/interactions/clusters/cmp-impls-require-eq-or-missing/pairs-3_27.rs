#[derive(Clone, Copy, PartialEq, Debug)]
struct P { x: i64, y: i64 }
fn main() { let a = [P { x: 1, y: 2 }, P { x: 3, y: 4 }]; let b = a; println!("{}", a == b); }
