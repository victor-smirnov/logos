fn g<A: Into<i64>>(a: A) -> i64 { a.into() }
fn main() { println!("{} {}", g(5i64), g(3i32)); }
