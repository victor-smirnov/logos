// exhaustiveness oracle battery (ADR 0030 S3): i64 {MIN..=0, 1..=MAX}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i64 = 500i64; let r: i32 = match x { i64::MIN..=0i64 => 1i32, 1i64..=i64::MAX => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
