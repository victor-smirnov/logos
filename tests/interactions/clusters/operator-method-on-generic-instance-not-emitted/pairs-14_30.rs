#[derive(Clone, PartialEq)]
struct Pair<T> { a: T, b: T }
fn main() { let p = Pair { a: 3i64, b: 4 }; let q = p.clone(); println!("{}", p == q); }
