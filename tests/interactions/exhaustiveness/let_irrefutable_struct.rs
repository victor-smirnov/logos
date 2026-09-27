// exhaustiveness oracle battery (ADR 0030 S3): let S{a, b} = s;
#![allow(dead_code, unused_variables, unused_assignments)]
struct S { a: i64, b: i64 }
fn run() -> i32 { let s = S { a: 3i64, b: 4i64 }; let S { a, b } = s; return (a * 10i64 + b) as i32; }

fn main() { std::process::exit(run()); }
