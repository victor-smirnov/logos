fn less<T>(a: &T, b: &T) -> bool { a < b }
fn main() { let x: i64 = 3; let y: i64 = 8; if less::<i64>(&x, &y) { std::process::exit(1); } }
