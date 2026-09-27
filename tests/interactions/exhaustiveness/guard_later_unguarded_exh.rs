// exhaustiveness oracle battery (ADR 0030 S3): Option<i64> {Some(x) if x>0, Some(_), None}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let o: Option<i64> = Option::Some(-1i64); let r: i32 = match o { Option::Some(x) if x > 0i64 => 1i32, Option::Some(_) => 4i32, Option::None => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
