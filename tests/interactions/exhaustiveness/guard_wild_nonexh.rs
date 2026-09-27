// exhaustiveness oracle battery (ADR 0030 S3): i64 {_ if x>0} guarded wildcard only (value=-3)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i64 = -3i64; let r: i32 = match x { _ if x > 0i64 => 1i32 }; return r; }

fn main() { std::process::exit(run()); }
