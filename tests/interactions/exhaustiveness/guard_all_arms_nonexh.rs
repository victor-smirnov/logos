// exhaustiveness oracle battery (ADR 0030 S3): bool {true if t, false if t} every arm guarded
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let b: bool = true; let t: bool = true; let r: i32 = match b { true if t => 1i32, false if t => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
