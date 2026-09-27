// exhaustiveness oracle battery (ADR 0030 S3): bool {true if cond, false} (value=true, cond false)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let b: bool = true; let k: i64 = 0i64; let r: i32 = match b { true if k > 0i64 => 1i32, false => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
