#[derive(Clone, Copy, PartialEq)]
struct V2<T> { x: T, y: T }
fn main() { let a = V2 { x: 1i64, y: 2i64 }; let b = a; println!("{}", a == b); }
