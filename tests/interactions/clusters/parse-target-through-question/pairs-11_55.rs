fn f(s: &str) -> Result<i64, std::num::ParseIntError> { let v: i64 = s.parse()?; Ok(v * 2) }
fn main() { println!("{:?}", f("21").ok()); }
