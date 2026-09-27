// exhaustiveness oracle battery (ADR 0030 S3): match x {} over i64 (E0004)
#![allow(dead_code, unused_variables, unused_assignments)]
fn f(x: i64) -> i32 { match x {} }
fn run() -> i32 { return f(1i64); }

fn main() { std::process::exit(run()); }
