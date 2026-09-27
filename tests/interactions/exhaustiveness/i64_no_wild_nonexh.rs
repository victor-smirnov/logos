// exhaustiveness oracle battery (ADR 0030 S3): i64 {0, 1..=100} no wildcard (value=500)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i64 = 500i64; let r: i32 = match x { 0i64 => 1i32, 1i64..=100i64 => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
