// exhaustiveness oracle battery (ADR 0030 S3): Result<Option<i64>,i64> missing Ok(None) (value=Ok(None))
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let o: Result<Option<i64>, i64> = Result::Ok(Option::None); let r: i64 = match o { Result::Ok(Option::Some(x)) => x, Result::Err(e) => e }; return r as i32; }

fn main() { std::process::exit(run()); }
