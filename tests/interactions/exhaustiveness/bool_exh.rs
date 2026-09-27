// exhaustiveness oracle battery (ADR 0030 S3): match bool {true,false}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let b: bool = false; let r: i32 = match b { true => 10i32, false => 20i32 }; return r; }

fn main() { std::process::exit(run()); }
