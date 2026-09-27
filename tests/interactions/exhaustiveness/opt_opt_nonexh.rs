// exhaustiveness oracle battery (ADR 0030 S3): Option<Option<i64>> missing Some(None) (value=Some(None))
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let o: Option<Option<i64>> = Option::Some(Option::None); let r: i64 = match o { Option::Some(Option::Some(x)) => x, Option::None => 0i64 }; return r as i32; }

fn main() { std::process::exit(run()); }
