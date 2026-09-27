// exhaustiveness oracle battery (ADR 0030 S3): i64 {0, k} binding catch-all
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i64 = 9i64; let r: i64 = match x { 0i64 => 1i64, k => k + 1i64 }; return r as i32; }

fn main() { std::process::exit(run()); }
