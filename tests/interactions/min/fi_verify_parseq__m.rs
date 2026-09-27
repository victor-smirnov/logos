fn mk<T: Default>() -> Result<T, i32> { return Ok(T::default()); }
fn p() -> Result<i64, i32> { let n: i64 = mk()?; return Ok(n + 1); }
fn main() { println!("{}", p().unwrap()); }
