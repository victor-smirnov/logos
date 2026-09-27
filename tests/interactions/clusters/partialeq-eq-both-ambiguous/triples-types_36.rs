#[derive(PartialEq, Eq, PartialOrd, Ord)]
struct P { a: i64, b: i64 }
fn main() { let mut v = vec![P { a: 2, b: 1 }, P { a: 1, b: 5 }, P { a: 1, b: 2 }]; v.sort(); println!("{} {} {}", v[0].b, v[1].b, v[2].a); }
