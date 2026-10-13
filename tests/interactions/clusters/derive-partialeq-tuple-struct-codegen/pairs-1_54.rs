#[derive(PartialEq)]
struct M(i64, i64);
#[derive(PartialEq)]
struct N { v: i64, w: i64 }
fn main() { println!("{}", N { v: 1, w: 0 } == N { v: 2, w: 0 }); }
