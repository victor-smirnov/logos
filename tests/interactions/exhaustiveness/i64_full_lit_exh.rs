// exhaustiveness oracle battery (ADR 0030 S3): i64 {-9223372036854775808..=0, 1..=9223372036854775807} literal bounds
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i64 = 500i64; let r: i32 = match x { -9223372036854775808i64..=0i64 => 1i32, 1i64..=9223372036854775807i64 => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
