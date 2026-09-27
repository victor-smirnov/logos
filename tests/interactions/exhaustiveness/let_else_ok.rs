// exhaustiveness oracle battery (ADR 0030 S3): let Some(x) = o else { return 1 }; (value=None)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let o: Option<i64> = Option::None; let Option::Some(x) = o else { return 11i32; }; return x as i32; }

fn main() { std::process::exit(run()); }
