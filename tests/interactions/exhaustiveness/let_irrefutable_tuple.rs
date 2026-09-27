// exhaustiveness oracle battery (ADR 0030 S3): let (a, (b, c)) = t;
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let t = (1i64, (2i64, 3i64)); let (a, (b, c)) = t; return (a * 100i64 + b * 10i64 + c) as i32; }

fn main() { std::process::exit(run()); }
