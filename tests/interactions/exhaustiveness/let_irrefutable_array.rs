// exhaustiveness oracle battery (ADR 0030 S3): let [a, b, c] = [i64;3];
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let v: [i64; 3] = [1i64, 2i64, 3i64]; let [a, b, c] = v; return (a + b + c) as i32; }

fn main() { std::process::exit(run()); }
