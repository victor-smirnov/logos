// exhaustiveness oracle battery (ADR 0030 S3): let Some(x) = o else { 5i64 }; else must diverge (E0308)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let o: Option<i64> = Option::None; let Option::Some(x) = o else { 5i64 }; return x as i32; }

fn main() { std::process::exit(run()); }
