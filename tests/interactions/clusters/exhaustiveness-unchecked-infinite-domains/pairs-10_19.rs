struct P { x: i32, y: bool }
fn g(p: P) -> i32 { match p { P { x: 0, .. } => 0, P { y: true, .. } => 1 } }
fn main() { println!("{}", g(P { x: 5, y: false })); }
