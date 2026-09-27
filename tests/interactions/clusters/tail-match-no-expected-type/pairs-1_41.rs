fn f() -> Result<i64, i64> { Ok(0) }
fn g(b: bool) -> Result<i64, i64> { match b { true => Ok(1), false => Err(2) } }
fn h(b: bool) -> Option<i64> { if b { Some(1) } else { None } }
fn k(b: bool) -> Result<i64, i64> { match b { true => Ok(1), false => { let x: i64 = 5; Ok(x) } } }
fn main() { println!("{:?} {:?} {:?} {:?}", f(), g(false), h(true), k(true)); }
