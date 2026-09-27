// exhaustiveness oracle battery (ADR 0030 S3): Option<i64> {x @ Some(_), None}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let o: Option<i64> = Option::Some(4i64); let r: i32 = match o { x @ Option::Some(_) => { match x { Option::Some(v) => v as i32, Option::None => 0i32 } } Option::None => 0i32 }; return r; }

fn main() { std::process::exit(run()); }
