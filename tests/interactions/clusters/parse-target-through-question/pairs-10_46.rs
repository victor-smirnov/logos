fn p(s: &str) -> Result<i64, std::num::ParseIntError> { let v: i64 = s.parse()?; Ok(v + 1) }
fn main() { println!("{:?}", p("41").is_ok()); }
