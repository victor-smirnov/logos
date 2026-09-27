use std::num::ParseIntError;
fn p(s: &str) -> Result<i64, ParseIntError> { let n: i64 = s.parse()?; return Ok(n + 1); }
fn main() { println!("{}", p("41").unwrap()); }
