const LIMIT: i64 = 10;
fn f() -> i64 { const LIMIT: i64 = 100; LIMIT + 1 }
fn main() { println!("{} {}", f(), LIMIT); }
