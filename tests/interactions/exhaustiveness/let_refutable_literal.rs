// exhaustiveness oracle battery (ADR 0030 S3): let 0i64 = x; (E0005)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i64 = 5i64; let 0i64 = x; return 1i32; }

fn main() { std::process::exit(run()); }
