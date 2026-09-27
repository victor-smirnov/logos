#[derive(Clone, Copy)]
struct P { x: i64, s: String }
fn main() { let a = P { x: 1, s: String::from("heap") }; let b = a; println!("{} {} {}", a.x, a.s, b.s); }
