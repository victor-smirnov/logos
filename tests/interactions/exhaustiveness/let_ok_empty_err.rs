// exhaustiveness oracle battery (ADR 0030 S3): let Ok(v) = r; r: Result<i64,Empty> (irrefutable)
#![allow(dead_code, unused_variables, unused_assignments)]
enum Empty {}

fn run() -> i32 { let r: Result<i64, Empty> = Result::Ok(8i64); let Result::Ok(v) = r; return v as i32; }

fn main() { std::process::exit(run()); }
