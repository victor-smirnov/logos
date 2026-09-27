#[derive(Debug)]
struct R { a: i64, b: i64, d: i64 }
#[derive(Debug)]
struct S { a: i64, count: i64 }
fn main() { println!("{:?}", R { a: 1, b: 2, d: 3 }); println!("{:?}", S { a: 1, count: 2 }); }
