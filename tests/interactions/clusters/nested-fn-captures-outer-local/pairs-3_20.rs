#[derive(Clone, Copy, Debug)]
struct P { x: i64, y: i64 }
fn main() { let p = P { x: 1, y: 2 }; fn show() -> i64 { p.x } println!("{}", show()); }
