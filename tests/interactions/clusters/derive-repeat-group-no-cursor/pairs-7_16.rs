#[derive(Clone, Copy, PartialEq)]
struct M { amt: i64 }
#[derive(Clone, Copy, PartialEq)]
struct N { a: i64, b: i64 }
fn main() { println!("{} {}", M { amt: 1 } == M { amt: 1 }, N { a: 1, b: 2 } == N { a: 1, b: 3 }); }
