// exhaustiveness oracle battery (ADR 0030 S3): let (0i64, y) = t; (E0005)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let t = (5i64, 6i64); let (0i64, y) = t; return y as i32; }

fn main() { std::process::exit(run()); }
