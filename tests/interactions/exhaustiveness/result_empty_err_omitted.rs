// exhaustiveness oracle battery (ADR 0030 S3): Result<i64,Empty> {Ok(v)} Err omitted (min_exhaustive_patterns)
#![allow(dead_code, unused_variables, unused_assignments)]
enum Empty {}

fn g(x: Result<i64, Empty>) -> i64 { match x { Result::Ok(v) => v } }
fn run() -> i32 { return g(Result::Ok(6i64)) as i32; }

fn main() { std::process::exit(run()); }
