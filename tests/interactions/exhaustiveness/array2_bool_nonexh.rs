// exhaustiveness oracle battery (ADR 0030 S3): [bool;2] missing [f,t] (value=[f,t])
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let a: [bool; 2] = [false, true]; let r: i32 = match a { [true, _] => 1i32, [false, false] => 3i32 }; return r; }

fn main() { std::process::exit(run()); }
