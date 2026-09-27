// exhaustiveness oracle battery (ADR 0030 S3): match &Option<i64> {&Some(x), &None}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let o: Option<i64> = Option::Some(7i64); let r: i64 = match &o { &Option::Some(x) => x, &Option::None => 0i64 }; return r as i32; }

fn main() { std::process::exit(run()); }
