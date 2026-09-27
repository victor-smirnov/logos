// exhaustiveness oracle battery (ADR 0030 S3): i64 stmt-form {0,1} no wildcard, fallthrough return 99 (value=5)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i64 = 5i64; match x { 0i64 => { return 1i32; } 1i64 => { return 2i32; } } return 99i32; }

fn main() { std::process::exit(run()); }
