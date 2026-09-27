#[derive(PartialEq, Eq)]
struct Pt { x: i64, y: i64 }
fn main() { let a = Pt { x: 1, y: 2 }; let b = Pt { x: 1, y: 2 }; println!("{}", a == b); }
