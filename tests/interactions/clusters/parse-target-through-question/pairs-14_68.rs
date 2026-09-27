fn f(s: &str) -> Result<i64, std::num::ParseIntError> { let n: i64 = s.parse()?; Ok(n + 1) }
fn main() { println!("{:?}", f("41").is_ok()); }
